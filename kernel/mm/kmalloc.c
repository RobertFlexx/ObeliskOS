/*
 * Obelisk OS - Kernel Memory Allocator
 * From Axioms, Order.
 *
 * Implements a slab allocator for kernel memory management.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <mm/kmalloc.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <obelisk/zig_safe_arith.h>

/* Size classes for general-purpose allocation */
static const size_t size_classes[] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
};
#define NUM_SIZE_CLASSES (sizeof(size_classes) / sizeof(size_classes[0]))

/* Caches for each size class */
static struct kmem_cache *size_caches[NUM_SIZE_CLASSES];

/* Global cache list */
static LIST_HEAD(cache_list);

/* Statistics */
static uint64_t total_allocated = 0;
static uint64_t total_freed = 0;

/* Early boot allocator (bump allocator) */
static uint64_t early_heap_start = 0;
static uint64_t early_heap_end = 0;
static uint64_t early_heap_current = 0;
static bool slab_initialized = false;

/* ==========================================================================
 * Early Boot Allocator
 * ========================================================================== */

static void early_heap_init(void) {
    /* Use 1MB of memory for early allocations */
    early_heap_start = (uint64_t)PHYS_TO_VIRT(pmm_alloc_pages(256));
    if (!early_heap_start) {
        panic("Failed to allocate early heap");
    }
    early_heap_end = early_heap_start + (256 * PAGE_SIZE);
    early_heap_current = early_heap_start;
    
    printk(KERN_DEBUG "Early heap: 0x%lx - 0x%lx\n",
           early_heap_start, early_heap_end);
}

static void *early_alloc(size_t size) {
    size = ALIGN_UP(size, 16);
    
    if (early_heap_current + size > early_heap_end) {
        return NULL;
    }
    
    void *ptr = (void *)early_heap_current;
    early_heap_current += size;
    
    memset(ptr, 0, size);
    return ptr;
}

/* ==========================================================================
 * Slab Allocator
 * ========================================================================== */

/* Calculate objects per slab */
static uint32_t calc_objects_per_slab(size_t obj_size, size_t slab_size) {
    /* Account for freelist overhead */
    size_t usable = slab_size - sizeof(struct slab);
    return usable / (obj_size + sizeof(uint32_t));
}

/* Create a new slab */
static struct slab *slab_create(struct kmem_cache *cache) {
    /* Allocate slab pages */
    size_t pages = cache->slab_size / PAGE_SIZE;
    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys) {
        return NULL;
    }
    
    void *base = PHYS_TO_VIRT(phys);
    for (size_t i = 0; i < pages; i++) {
        uint64_t page_phys = phys + (i * PAGE_SIZE);
        pmm_page_set_owner(page_phys, PAGE_OWNER_SLAB);
        pmm_page_update_flags(page_phys, PAGE_FLAG_SLAB | PAGE_FLAG_KERNEL, 0);
    }
    
    /* Initialize slab structure (at the beginning of the slab) */
    struct slab *slab = (struct slab *)base;
    slab->base = (uint8_t *)base + sizeof(struct slab);
    slab->inuse = 0;
    slab->free_idx = 0;
    slab->cache = cache;
    
    /* Initialize freelist */
    slab->freelist = (uint32_t *)((uint8_t *)slab->base +
                     (cache->objects_per_slab * cache->aligned_size));
    
    for (uint32_t i = 0; i < cache->objects_per_slab; i++) {
        slab->freelist[i] = i + 1;
    }
    slab->freelist[cache->objects_per_slab - 1] = UINT32_MAX;  /* End marker */
    
    /* Call constructors if defined */
    if (cache->ctor) {
        for (uint32_t i = 0; i < cache->objects_per_slab; i++) {
            void *obj = (uint8_t *)slab->base + (i * cache->aligned_size);
            cache->ctor(obj);
        }
    }
    
    cache->total_slabs++;
    cache->total_objects += cache->objects_per_slab;
    
    return slab;
}

/* Destroy a slab */
static void slab_destroy(struct kmem_cache *cache, struct slab *slab) {
    /* Call destructors if defined */
    if (cache->dtor) {
        for (uint32_t i = 0; i < cache->objects_per_slab; i++) {
            void *obj = (uint8_t *)slab->base + (i * cache->aligned_size);
            cache->dtor(obj);
        }
    }
    
    /* Free slab pages */
    uint64_t phys = VIRT_TO_PHYS((uint64_t)slab);
    size_t pages = cache->slab_size / PAGE_SIZE;
    for (size_t i = 0; i < pages; i++) {
        uint64_t page_phys = phys + (i * PAGE_SIZE);
        pmm_page_update_flags(page_phys, 0, PAGE_FLAG_SLAB);
    }
    pmm_free_pages(phys, pages);
    
    cache->total_slabs--;
    cache->total_objects -= cache->objects_per_slab;
}

/* Allocate from slab */
static void *slab_alloc_obj(struct slab *slab) {
    if (slab->free_idx == UINT32_MAX) {
        return NULL;  /* Slab is full */
    }
    
    uint32_t idx = slab->free_idx;
    slab->free_idx = slab->freelist[idx];
    slab->inuse++;
    
    void *obj = (uint8_t *)slab->base + (idx * slab->cache->aligned_size);
    
    return obj;
}

/* Free to slab */
static void slab_free_obj(struct slab *slab, void *obj) {
    uint64_t offset = (uint64_t)obj - (uint64_t)slab->base;
    uint32_t idx = offset / slab->cache->aligned_size;
    
    slab->freelist[idx] = slab->free_idx;
    slab->free_idx = idx;
    slab->inuse--;
}

static bool slab_contains_obj(struct kmem_cache *cache, struct slab *slab, void *obj) {
    if (!cache || !slab || !obj) {
        return false;
    }
    uintptr_t start = (uintptr_t)slab->base;
    uintptr_t end = start + (cache->objects_per_slab * cache->aligned_size);
    uintptr_t p = (uintptr_t)obj;
    if (p < start || p >= end) {
        return false;
    }
    return ((p - start) % cache->aligned_size) == 0;
}

static struct slab *find_slab_in_list(struct kmem_cache *cache,
                                      struct list_head *head, void *obj) {
    struct list_head *pos;
    list_for_each(pos, head) {
        struct slab *slab = list_entry(pos, struct slab, list);
        if (slab_contains_obj(cache, slab, obj)) {
            return slab;
        }
    }
    return NULL;
}

static struct slab *find_slab_in_cache(struct kmem_cache *cache, void *obj) {
    struct slab *slab = find_slab_in_list(cache, &cache->slabs_partial, obj);
    if (slab) return slab;
    slab = find_slab_in_list(cache, &cache->slabs_full, obj);
    if (slab) return slab;
    return find_slab_in_list(cache, &cache->slabs_free, obj);
}

static struct kmem_cache *find_owner_cache_for_obj(void *obj, struct slab **out_slab) {
    struct list_head *pos;
    list_for_each(pos, &cache_list) {
        struct kmem_cache *cache = list_entry(pos, struct kmem_cache, list);
        struct slab *slab = find_slab_in_cache(cache, obj);
        if (slab) {
            if (out_slab) {
                *out_slab = slab;
            }
            return cache;
        }
    }
    return NULL;
}

/* ==========================================================================
 * Cache Operations
 * ========================================================================== */

/* Create a new cache */
struct kmem_cache *kmem_cache_create(const char *name, size_t size,
                                     size_t align, uint32_t flags,
                                     void (*ctor)(void *)) {
    struct kmem_cache *cache;
    
    if (slab_initialized) {
        cache = kmalloc(sizeof(struct kmem_cache));
    } else {
        cache = early_alloc(sizeof(struct kmem_cache));
    }
    
    if (!cache) {
        return NULL;
    }
    
    /* Calculate alignment */
    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }
    if (flags & SLAB_HWCACHE_ALIGN) {
        align = MAX(align, 64);  /* Cache line size */
    }
    
    cache->name = name;
    cache->object_size = size;
    cache->aligned_size = ALIGN_UP(size, align);
    cache->align = align;
    cache->flags = flags;
    cache->ctor = ctor;
    cache->dtor = NULL;
    
    /* Determine slab size */
    if (cache->aligned_size <= PAGE_SIZE / 8) {
        cache->slab_size = PAGE_SIZE;
    } else if (cache->aligned_size <= PAGE_SIZE) {
        cache->slab_size = PAGE_SIZE * 2;
    } else {
        cache->slab_size = ALIGN_UP(cache->aligned_size + sizeof(struct slab),
                                    PAGE_SIZE);
    }
    
    cache->objects_per_slab = calc_objects_per_slab(cache->aligned_size,
                                                     cache->slab_size);
    if (cache->objects_per_slab == 0) {
        printk(KERN_ERR "kmalloc: cache '%s' objects_per_slab=0 (obj=%zu slab=%zu)\n",
               name ? name : "<unnamed>", cache->aligned_size, cache->slab_size);
        return NULL;
    }
    
    INIT_LIST_HEAD(&cache->slabs_full);
    INIT_LIST_HEAD(&cache->slabs_partial);
    INIT_LIST_HEAD(&cache->slabs_free);
    
    cache->total_objects = 0;
    cache->active_objects = 0;
    cache->total_slabs = 0;
    cache->lock = (spinlock_t)SPINLOCK_INIT;
    
    /* Add to global cache list */
    list_add(&cache->list, &cache_list);
    
    printk(KERN_DEBUG "Created cache '%s': obj_size=%lu, aligned=%lu, per_slab=%u\n",
           name, size, cache->aligned_size, cache->objects_per_slab);
    
    return cache;
}

/* Destroy a cache */
void kmem_cache_destroy(struct kmem_cache *cache) {
    /* Free all slabs */
    struct list_head *pos, *tmp;
    
    list_for_each_safe(pos, tmp, &cache->slabs_full) {
        struct slab *slab = list_entry(pos, struct slab, list);
        list_del(pos);
        slab_destroy(cache, slab);
    }
    
    list_for_each_safe(pos, tmp, &cache->slabs_partial) {
        struct slab *slab = list_entry(pos, struct slab, list);
        list_del(pos);
        slab_destroy(cache, slab);
    }
    
    list_for_each_safe(pos, tmp, &cache->slabs_free) {
        struct slab *slab = list_entry(pos, struct slab, list);
        list_del(pos);
        slab_destroy(cache, slab);
    }
    
    /* Remove from global list */
    list_del(&cache->list);
    
    kfree(cache);
}

/* Allocate from cache */
void *kmem_cache_alloc(struct kmem_cache *cache) {
    struct slab *slab = NULL;
    void *obj = NULL;
    
    /* Try partial slabs first */
    if (!list_empty(&cache->slabs_partial)) {
        slab = list_first_entry(&cache->slabs_partial, struct slab, list);
    }
    
    /* Try free slabs */
    if (!slab && !list_empty(&cache->slabs_free)) {
        slab = list_first_entry(&cache->slabs_free, struct slab, list);
        list_del(&slab->list);
        list_add(&slab->list, &cache->slabs_partial);
    }
    
    /* Create new slab */
    if (!slab) {
        slab = slab_create(cache);
        if (!slab) {
            return NULL;
        }
        list_add(&slab->list, &cache->slabs_partial);
    }
    
    /* Allocate object */
    obj = slab_alloc_obj(slab);
    cache->active_objects++;
    total_allocated += cache->object_size;
    
    /* Move slab to full list if necessary */
    if (slab->inuse == cache->objects_per_slab) {
        list_del(&slab->list);
        list_add(&slab->list, &cache->slabs_full);
    }
    
    return obj;
}

/* Allocate and zero from cache */
void *kmem_cache_zalloc(struct kmem_cache *cache) {
    void *obj = kmem_cache_alloc(cache);
    if (obj) {
        memset(obj, 0, cache->object_size);
    }
    return obj;
}

/* Free to cache */
void kmem_cache_free(struct kmem_cache *cache, void *obj) {
    if (!cache || !obj) return;
    if (cache->aligned_size == 0 || cache->objects_per_slab == 0) {
        return;
    }

    struct kmem_cache *target_cache = cache;
    struct slab *slab = find_slab_in_cache(target_cache, obj);
    if (!slab) {
        /* Be resilient if caller provided the wrong cache pointer. */
        target_cache = find_owner_cache_for_obj(obj, &slab);
        if (!target_cache || !slab) {
            return;
        }
    }
    if (slab->inuse == 0) {
        return;
    }

    bool was_full = (slab->inuse == target_cache->objects_per_slab);
    
    /* Free object */
    slab_free_obj(slab, obj);
    if (target_cache->active_objects > 0) {
        target_cache->active_objects--;
    }
    total_freed += target_cache->object_size;
    
    /* Move slab between lists */
    if (was_full) {
        list_del(&slab->list);
        list_add(&slab->list, &target_cache->slabs_partial);
    } else if (slab->inuse == 0) {
        list_del(&slab->list);
        list_add(&slab->list, &target_cache->slabs_free);
    }
}

/* Shrink cache */
int kmem_cache_shrink(struct kmem_cache *cache) {
    int freed = 0;
    struct list_head *pos, *tmp;
    
    list_for_each_safe(pos, tmp, &cache->slabs_free) {
        struct slab *slab = list_entry(pos, struct slab, list);
        list_del(pos);
        slab_destroy(cache, slab);
        freed++;
    }
    
    return freed;
}

/* ==========================================================================
 * General Purpose Allocation
 * ========================================================================== */

/* Find appropriate size class */
static int find_size_class(size_t size) {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= size_classes[i]) {
            return (int)i;
        }
    }
    return -1;
}

/* Allocate memory */
void *kmalloc(size_t size) {
    if (!slab_initialized) {
        return early_alloc(size);
    }
    
    if (size == 0) {
        return NULL;
    }
    
    int class = find_size_class(size);
    
    if (class >= 0) {
        /* Use slab allocator */
        return kmem_cache_alloc(size_caches[class]);
    }
    
    /* Large allocation - use page allocator directly */
    {
        uint64_t need_u64;
        size_t aligned;
        size_t pages;

        if (zig_u64_add_ok((uint64_t)size, (uint64_t)sizeof(size_t), &need_u64) != 0 ||
            need_u64 > (uint64_t)SIZE_MAX) {
            return NULL;
        }
        {
            uint64_t aligned64;

            if (zig_u64_align_up_pow2_ok(need_u64, PAGE_SIZE, &aligned64) != 0 ||
                aligned64 > (uint64_t)SIZE_MAX) {
                return NULL;
            }
            aligned = (size_t)aligned64;
        }
        pages = aligned / PAGE_SIZE;
        uint64_t phys = pmm_alloc_pages(pages);
        if (!phys) {
            return NULL;
        }

        {
            void *ptr = PHYS_TO_VIRT(phys);

            /* Store size for kfree */
            *(size_t *)ptr = size;

            return (uint8_t *)ptr + sizeof(size_t);
        }
    }
}

/* Allocate and zero memory */
void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/* Allocate array */
void *kcalloc(size_t n, size_t size) {
    uint64_t total;

    if (zig_u64_mul_ok((uint64_t)n, (uint64_t)size, &total) != 0) {
        return NULL;
    }
    if (total > (uint64_t)SIZE_MAX) {
        return NULL;
    }

    return kzalloc((size_t)total);
}

/* Reallocate memory */
void *krealloc(void *ptr, size_t size) {
    if (!ptr) {
        return kmalloc(size);
    }
    
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    /* For simplicity, always allocate new block and copy */
    void *new_ptr = kmalloc(size);
    if (!new_ptr) {
        return NULL;
    }
    
    size_t old_size = kmalloc_usable_size(ptr);
    memcpy(new_ptr, ptr, MIN(old_size, size));
    kfree(ptr);
    
    return new_ptr;
}

/* Free memory */
void kfree(void *ptr) {
    if (!ptr) return;
    
    if (!slab_initialized) {
        /* Early allocations cannot be freed */
        return;
    }
    
    /* Check if it's a slab allocation */
    /* TODO: Better way to determine this */
    
    /* For now, try each size class */
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        /* Check if address falls within any slab */
        /* This is a simplified check */
        kmem_cache_free(size_caches[i], ptr);
        return;
    }
    
    /* Large allocation */
    size_t *size_ptr = (size_t *)((uint8_t *)ptr - sizeof(size_t));
    size_t size = *size_ptr;
    size_t pages = ALIGN_UP(size + sizeof(size_t), PAGE_SIZE) / PAGE_SIZE;
    
    uint64_t phys = VIRT_TO_PHYS((uint64_t)size_ptr);
    pmm_free_pages(phys, pages);
}

/* Get usable size */
size_t kmalloc_usable_size(void *ptr) {
    if (!ptr) return 0;
    
    /* Check size classes */
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        /* Simplified check */
        return size_classes[i];
    }
    
    /* Large allocation */
    size_t *size_ptr = (size_t *)((uint8_t *)ptr - sizeof(size_t));
    return *size_ptr;
}

/* ==========================================================================
 * Statistics
 * ========================================================================== */

void kmalloc_dump_stats(void) {
    printk(KERN_INFO "Kernel Memory Statistics:\n");
    printk(KERN_INFO "  Total allocated: %lu bytes\n", total_allocated);
    printk(KERN_INFO "  Total freed: %lu bytes\n", total_freed);
    printk(KERN_INFO "  Active: %lu bytes\n", total_allocated - total_freed);
    
    printk(KERN_INFO "  Caches:\n");
    struct list_head *pos;
    list_for_each(pos, &cache_list) {
        struct kmem_cache *cache = list_entry(pos, struct kmem_cache, list);
        printk(KERN_INFO "    %s: %lu active / %lu total objects, %lu slabs\n",
               cache->name, cache->active_objects, cache->total_objects,
               cache->total_slabs);
    }
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void kmalloc_init(void) {
    printk(KERN_INFO "Initializing kernel memory allocator...\n");
    
    /* Initialize early heap */
    early_heap_init();
    
    /* Create caches for each size class */
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "kmalloc-%lu", size_classes[i]);
        
        size_caches[i] = kmem_cache_create(name, size_classes[i],
                                           sizeof(void *), 0, NULL);
        if (!size_caches[i]) {
            panic("Failed to create kmalloc cache for size %lu",
                  size_classes[i]);
        }
    }
    
    slab_initialized = true;
    
    printk(KERN_INFO "Slab allocator initialized with %d size classes\n",
           (int)NUM_SIZE_CLASSES);
}