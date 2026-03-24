/*
 * Obelisk OS - Kernel Memory Allocator Header
 * From Axioms, Order.
 */

#ifndef _MM_KMALLOC_H
#define _MM_KMALLOC_H

#include <obelisk/types.h>

/* Allocation flags */
#define GFP_KERNEL      0x00    /* Normal kernel allocation */
#define GFP_ATOMIC      0x01    /* Cannot sleep */
#define GFP_ZERO        0x02    /* Zero memory */
#define GFP_DMA         0x04    /* DMA-capable memory */
#define GFP_USER        0x08    /* User allocation */
#define GFP_NOWAIT      0x10    /* Don't wait */
#define GFP_NORETRY     0x20    /* Don't retry on failure */

/* Slab cache flags */
#define SLAB_HWCACHE_ALIGN  0x0001  /* Align to cache line */
#define SLAB_POISON         0x0002  /* Poison freed objects */
#define SLAB_RED_ZONE       0x0004  /* Add red zones */
#define SLAB_PANIC          0x0008  /* Panic on allocation failure */
#define SLAB_RECLAIM        0x0010  /* Reclaimable */

/* Slab structure */
struct slab {
    struct list_head list;      /* List of slabs */
    void *base;                 /* Base address */
    uint32_t inuse;             /* Objects in use */
    uint32_t free_idx;          /* First free object index */
    uint8_t *freelist;          /* Free object list */
    struct kmem_cache *cache;   /* Parent cache */
};

/* Slab cache structure */
struct kmem_cache {
    const char *name;           /* Cache name */
    size_t object_size;         /* Size of each object */
    size_t aligned_size;        /* Aligned object size */
    size_t align;               /* Object alignment */
    uint32_t flags;             /* Cache flags */
    uint32_t objects_per_slab;  /* Objects per slab */
    size_t slab_size;           /* Size of each slab */
    
    void (*ctor)(void *);       /* Object constructor */
    void (*dtor)(void *);       /* Object destructor */
    
    struct list_head slabs_full;    /* Full slabs */
    struct list_head slabs_partial; /* Partial slabs */
    struct list_head slabs_free;    /* Free slabs */
    
    uint64_t total_objects;     /* Total objects allocated */
    uint64_t active_objects;    /* Currently active objects */
    uint64_t total_slabs;       /* Total slabs */
    
    spinlock_t lock;            /* Cache lock */
    struct list_head list;      /* Global cache list */
};

/* Initialization */
void kmalloc_init(void);

/* General allocation */
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *kcalloc(size_t n, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

/* Sized allocation (with flags) */
void *kmalloc_flags(size_t size, uint32_t flags);

/* Slab cache operations */
struct kmem_cache *kmem_cache_create(const char *name, size_t size,
                                     size_t align, uint32_t flags,
                                     void (*ctor)(void *));
void kmem_cache_destroy(struct kmem_cache *cache);
void *kmem_cache_alloc(struct kmem_cache *cache);
void *kmem_cache_zalloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);

/* Cache shrinking */
int kmem_cache_shrink(struct kmem_cache *cache);
void kmem_cache_shrink_all(void);

/* Statistics and debugging */
void kmalloc_dump_stats(void);
size_t kmalloc_usable_size(void *ptr);

/* Debug helpers */
#ifdef CONFIG_DEBUG_KMALLOC
void kmalloc_check_corruption(void);
void kmalloc_trace_enable(void);
void kmalloc_trace_disable(void);
#else
#define kmalloc_check_corruption() do {} while(0)
#define kmalloc_trace_enable() do {} while(0)
#define kmalloc_trace_disable() do {} while(0)
#endif

#endif /* _MM_KMALLOC_H */