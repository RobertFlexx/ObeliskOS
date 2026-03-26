/*
 * Obelisk OS - Physical Memory Manager
 * From Axioms, Order.
 *
 * Implements a bitmap-based physical page allocator with zone support.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <mm/pmm.h>

/* External from physmem.c */
extern void physmem_init(uint32_t magic, void *mbi);

/* Memory zones */
static struct pmm_zone zones[ZONE_COUNT] = {
    [ZONE_DMA] = {
        .name = "DMA",
        .start_pfn = 0,
        .end_pfn = 0x1000,      /* 0-16MB */
    },
    [ZONE_NORMAL] = {
        .name = "Normal",
        .start_pfn = 0x1000,
        .end_pfn = 0x100000,    /* 16MB-4GB */
    },
    [ZONE_HIGH] = {
        .name = "High",
        .start_pfn = 0x100000,
        .end_pfn = 0,           /* Set during init */
    },
};

/* Global statistics */
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t usable_pages = 0;
static struct page *page_array = NULL;
static uint64_t page_array_count = 0;
static uint64_t page_array_backing_phys = 0;
static uint32_t initial_bitmap[1024 * 1024 / 32]; /* 1M pages = 4GB */

/* Bitmap operations */
static inline void bitmap_set(uint32_t *bitmap, uint64_t bit) {
    bitmap[bit / 32] |= (1U << (bit % 32));
}

static inline void bitmap_clear(uint32_t *bitmap, uint64_t bit) {
    bitmap[bit / 32] &= ~(1U << (bit % 32));
}

static inline bool bitmap_test(uint32_t *bitmap, uint64_t bit) {
    return (bitmap[bit / 32] & (1U << (bit % 32))) != 0;
}

/* Find first free bit in bitmap */
static int64_t bitmap_find_free(uint32_t *bitmap, uint64_t start, uint64_t end) {
    for (uint64_t i = start; i < end; i++) {
        if (!bitmap_test(bitmap, i)) {
            return i;
        }
    }
    return -1;
}

/* Find contiguous free bits */
static int64_t bitmap_find_free_region(uint32_t *bitmap, uint64_t start,
                                       uint64_t end, size_t count) {
    uint64_t found_start = 0;
    size_t found_count = 0;
    
    for (uint64_t i = start; i < end; i++) {
        if (!bitmap_test(bitmap, i)) {
            if (found_count == 0) {
                found_start = i;
            }
            found_count++;
            if (found_count == count) {
                return found_start;
            }
        } else {
            found_count = 0;
        }
    }
    
    return -1;
}

/* Get zone for a given PFN */
static struct pmm_zone *pfn_to_zone(uint64_t pfn) {
    for (int i = 0; i < ZONE_COUNT; i++) {
        if (pfn >= zones[i].start_pfn && pfn < zones[i].end_pfn) {
            return &zones[i];
        }
    }
    return NULL;
}

static inline struct page *pfn_to_page_struct(uint64_t pfn) {
    if (!page_array || pfn >= page_array_count) {
        return NULL;
    }
    return &page_array[pfn];
}

static void pmm_init_page_metadata(void) {
    size_t meta_bytes;
    size_t meta_pages;
    uint64_t phys;

    if (total_pages == 0) {
        return;
    }

    meta_bytes = total_pages * sizeof(struct page);
    meta_pages = ALIGN_UP(meta_bytes, PAGE_SIZE) / PAGE_SIZE;
    phys = pmm_alloc_pages(meta_pages);
    if (!phys) {
        panic("PMM: failed to allocate struct page metadata");
    }

    page_array = (struct page *)PHYS_TO_VIRT(phys);
    page_array_count = total_pages;
    page_array_backing_phys = phys;
    memset(page_array, 0, meta_pages * PAGE_SIZE);

    for (uint64_t pfn = 0; pfn < total_pages; pfn++) {
        struct pmm_zone *zone = pfn_to_zone(pfn);
        struct page *pg = &page_array[pfn];
        pg->zone = zone ? (uint16_t)(zone - &zones[0]) : (uint16_t)ZONE_NORMAL;
        pg->flags = PAGE_FLAG_RESERVED;
        pg->refcount = 1;
        pg->owner = PAGE_OWNER_KERNEL;
        if (zone == &zones[ZONE_DMA]) {
            pg->flags |= PAGE_FLAG_DMA;
        }
    }

    for (size_t i = 0; i < meta_pages; i++) {
        uint64_t pfn = phys_to_pfn(phys + (i * PAGE_SIZE));
        struct page *pg = pfn_to_page_struct(pfn);
        if (pg) {
            pg->flags |= PAGE_FLAG_KERNEL;
            pg->owner = PAGE_OWNER_KERNEL;
            pg->refcount = 1;
        }
    }

    printk(KERN_INFO "PMM: struct page metadata at phys=0x%lx (%zu pages, %zu bytes)\n",
           phys, meta_pages, meta_bytes);
}

/* Initialize PMM bitmap */
void pmm_init_bitmap(uint64_t num_pages) {
    const uint64_t max_bitmap_pages = (uint64_t)(sizeof(initial_bitmap) * 8);
    if (num_pages > max_bitmap_pages) {
        printk(KERN_WARNING "PMM: detected %lu pages, but bitmap supports %lu pages; capping managed range\n",
               num_pages, max_bitmap_pages);
        total_pages = max_bitmap_pages;
    } else {
        total_pages = num_pages;
    }
    
    /* Calculate bitmap size (1 bit per page) */
    size_t bitmap_pages = ALIGN_UP(num_pages / 8, PAGE_SIZE) / PAGE_SIZE;
    (void)bitmap_pages;
    
    /* Update zone boundaries */
    if (total_pages > 0x100000) {
        zones[ZONE_HIGH].end_pfn = total_pages;
    } else if (total_pages > 0x1000) {
        zones[ZONE_NORMAL].end_pfn = total_pages;
        zones[ZONE_HIGH].end_pfn = zones[ZONE_HIGH].start_pfn;
    } else {
        zones[ZONE_DMA].end_pfn = total_pages;
        zones[ZONE_NORMAL].end_pfn = zones[ZONE_NORMAL].start_pfn;
        zones[ZONE_HIGH].end_pfn = zones[ZONE_HIGH].start_pfn;
    }
    
    /* For initial setup, we use a static area in BSS */
    /* This will be replaced with proper memory once we can allocate */
    /* Initialize all zones with the same bitmap for now */
    for (int i = 0; i < ZONE_COUNT; i++) {
        zones[i].bitmap = initial_bitmap;
        zones[i].bitmap_size = sizeof(initial_bitmap);
        zones[i].total_pages = zones[i].end_pfn - zones[i].start_pfn;
        zones[i].free_pages = 0;
        zones[i].lock = (spinlock_t)SPINLOCK_INIT;
    }
    
    /* Mark all pages as used initially */
    memset(initial_bitmap, 0xFF, sizeof(initial_bitmap));
    free_pages = 0;
    usable_pages = 0;
    
    printk(KERN_INFO "PMM: Bitmap initialized for %lu pages (%lu MB)\n",
           total_pages, (total_pages * PAGE_SIZE) / (1024 * 1024));
}

/* Mark a region as free */
void pmm_mark_region_free(uint64_t base, uint64_t size) {
    uint64_t start_pfn = phys_to_pfn(ALIGN_UP(base, PAGE_SIZE));
    uint64_t end_pfn = phys_to_pfn(ALIGN_DOWN(base + size, PAGE_SIZE));
    
    if (end_pfn <= start_pfn) return;
    
    for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        struct pmm_zone *zone = pfn_to_zone(pfn);
        if (zone && bitmap_test(zone->bitmap, pfn)) {
            bitmap_clear(zone->bitmap, pfn);
            zone->free_pages++;
            free_pages++;
            usable_pages++;
            struct page *pg = pfn_to_page_struct(pfn);
            if (pg) {
                pg->refcount = 0;
                pg->flags &= ~(PAGE_FLAG_RESERVED | PAGE_FLAG_KERNEL | PAGE_FLAG_USER | PAGE_FLAG_SLAB);
                pg->owner = PAGE_OWNER_FREE;
                if (zone == &zones[ZONE_DMA]) {
                    pg->flags |= PAGE_FLAG_DMA;
                } else {
                    pg->flags &= ~PAGE_FLAG_DMA;
                }
            }
        }
    }
}

/* Mark a region as used */
void pmm_mark_region_used(uint64_t base, uint64_t size) {
    uint64_t start_pfn = phys_to_pfn(ALIGN_DOWN(base, PAGE_SIZE));
    uint64_t end_pfn = phys_to_pfn(ALIGN_UP(base + size, PAGE_SIZE));
    
    for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        struct pmm_zone *zone = pfn_to_zone(pfn);
        if (zone && !bitmap_test(zone->bitmap, pfn)) {
            bitmap_set(zone->bitmap, pfn);
            if (zone->free_pages > 0) zone->free_pages--;
            if (free_pages > 0) free_pages--;
            struct page *pg = pfn_to_page_struct(pfn);
            if (pg) {
                pg->refcount = 1;
                pg->flags |= PAGE_FLAG_RESERVED | PAGE_FLAG_KERNEL;
                pg->owner = PAGE_OWNER_KERNEL;
                if (zone == &zones[ZONE_DMA]) {
                    pg->flags |= PAGE_FLAG_DMA;
                } else {
                    pg->flags &= ~PAGE_FLAG_DMA;
                }
            }
        }
    }
}

/* Allocate a single page from a specific zone */
uint64_t pmm_alloc_page_zone(zone_type_t zone_type) {
    struct pmm_zone *zone = &zones[zone_type];
    
    if (zone->free_pages == 0) {
        return 0;
    }
    
    /* Find a free page */
    int64_t pfn = bitmap_find_free(zone->bitmap, zone->start_pfn, zone->end_pfn);
    if (pfn < 0) {
        return 0;
    }
    
    /* Mark as used */
    bitmap_set(zone->bitmap, pfn);
    zone->free_pages--;
    free_pages--;

    struct page *pg = pfn_to_page_struct((uint64_t)pfn);
    if (pg) {
        pg->refcount = 1;
        pg->flags |= PAGE_FLAG_RESERVED | PAGE_FLAG_KERNEL;
        pg->owner = (zone_type == ZONE_DMA) ? PAGE_OWNER_DMA : PAGE_OWNER_KERNEL;
        if (zone_type == ZONE_DMA) {
            pg->flags |= PAGE_FLAG_DMA;
        } else {
            pg->flags &= ~PAGE_FLAG_DMA;
        }
    }
    
    return pfn_to_phys(pfn);
}

/* Allocate a single page (any zone) */
uint64_t pmm_alloc_page(void) {
    uint64_t page;
    
    /* Try zones in order: Normal, High, DMA */
    page = pmm_alloc_page_zone(ZONE_NORMAL);
    if (page) return page;
    
    page = pmm_alloc_page_zone(ZONE_HIGH);
    if (page) return page;
    
    page = pmm_alloc_page_zone(ZONE_DMA);
    return page;
}

/* Allocate contiguous pages from a specific zone */
uint64_t pmm_alloc_pages_zone(size_t count, zone_type_t zone_type) {
    struct pmm_zone *zone = &zones[zone_type];
    
    if (zone->free_pages < count) {
        return 0;
    }
    
    /* Find contiguous free pages */
    int64_t start_pfn = bitmap_find_free_region(zone->bitmap, zone->start_pfn,
                                                 zone->end_pfn, count);
    if (start_pfn < 0) {
        return 0;
    }
    
    /* Mark as used */
    for (size_t i = 0; i < count; i++) {
        bitmap_set(zone->bitmap, start_pfn + i);
        struct page *pg = pfn_to_page_struct((uint64_t)start_pfn + i);
        if (pg) {
            pg->refcount = 1;
            pg->flags |= PAGE_FLAG_RESERVED | PAGE_FLAG_KERNEL;
            pg->owner = (zone_type == ZONE_DMA) ? PAGE_OWNER_DMA : PAGE_OWNER_KERNEL;
            if (zone_type == ZONE_DMA) {
                pg->flags |= PAGE_FLAG_DMA;
            } else {
                pg->flags &= ~PAGE_FLAG_DMA;
            }
        }
    }
    zone->free_pages -= count;
    free_pages -= count;
    
    return pfn_to_phys(start_pfn);
}

/* Allocate contiguous pages (any zone) */
uint64_t pmm_alloc_pages(size_t count) {
    uint64_t pages;
    
    pages = pmm_alloc_pages_zone(count, ZONE_NORMAL);
    if (pages) return pages;
    
    pages = pmm_alloc_pages_zone(count, ZONE_HIGH);
    if (pages) return pages;
    
    pages = pmm_alloc_pages_zone(count, ZONE_DMA);
    return pages;
}

/* Free a single page */
void pmm_free_page(uint64_t phys) {
    uint64_t pfn = phys_to_pfn(phys);
    struct pmm_zone *zone = pfn_to_zone(pfn);
    
    if (!zone) {
        printk(KERN_WARNING "PMM: Attempt to free invalid page 0x%lx\n", phys);
        return;
    }
    
    if (!bitmap_test(zone->bitmap, pfn)) {
        printk(KERN_WARNING "PMM: Double free of page 0x%lx\n", phys);
        return;
    }
    
    bitmap_clear(zone->bitmap, pfn);
    zone->free_pages++;
    free_pages++;

    struct page *pg = pfn_to_page_struct(pfn);
    if (pg) {
        pg->refcount = 0;
        pg->flags &= ~(PAGE_FLAG_RESERVED | PAGE_FLAG_KERNEL | PAGE_FLAG_USER | PAGE_FLAG_SLAB);
        pg->owner = PAGE_OWNER_FREE;
        if (zone == &zones[ZONE_DMA]) {
            pg->flags |= PAGE_FLAG_DMA;
        } else {
            pg->flags &= ~PAGE_FLAG_DMA;
        }
    }
}

/* Free contiguous pages */
void pmm_free_pages(uint64_t phys, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(phys + (i * PAGE_SIZE));
    }
}

/* Get total pages */
uint64_t pmm_get_total_pages(void) {
    return total_pages;
}

uint64_t pmm_get_usable_pages(void) {
    return usable_pages;
}

/* Get free pages */
uint64_t pmm_get_free_pages(void) {
    return free_pages;
}

/* Get used pages */
uint64_t pmm_get_used_pages(void) {
    return total_pages - free_pages;
}

/* Get zone */
struct pmm_zone *pmm_get_zone(zone_type_t type) {
    if (type >= ZONE_COUNT) return NULL;
    return &zones[type];
}

struct page *pmm_get_page_by_pfn(uint64_t pfn) {
    return pfn_to_page_struct(pfn);
}

struct page *pmm_get_page_by_phys(uint64_t phys) {
    return pfn_to_page_struct(phys_to_pfn(phys));
}

void pmm_page_set_owner(uint64_t phys, uint16_t owner) {
    struct page *pg = pmm_get_page_by_phys(phys);
    if (!pg) return;
    pg->owner = owner;
}

void pmm_page_update_flags(uint64_t phys, uint32_t set_flags, uint32_t clear_flags) {
    struct page *pg = pmm_get_page_by_phys(phys);
    if (!pg) return;
    pg->flags |= set_flags;
    pg->flags &= ~clear_flags;
}

void pmm_page_ref_inc(uint64_t phys) {
    struct page *pg = pmm_get_page_by_phys(phys);
    if (!pg) return;
    pg->refcount++;
}

bool pmm_page_ref_dec(uint64_t phys) {
    struct page *pg = pmm_get_page_by_phys(phys);
    if (!pg) return false;
    if (pg->refcount == 0) return false;
    pg->refcount--;
    return (pg->refcount == 0);
}

/* Dump statistics */
void pmm_dump_stats(void) {
    printk(KERN_INFO "Physical Memory Statistics:\n");
    printk(KERN_INFO "  Total: %lu pages (%lu MB)\n",
           total_pages, (total_pages * PAGE_SIZE) / (1024 * 1024));
    printk(KERN_INFO "  Free:  %lu pages (%lu MB)\n",
           free_pages, (free_pages * PAGE_SIZE) / (1024 * 1024));
    printk(KERN_INFO "  Used:  %lu pages (%lu MB)\n",
           total_pages - free_pages,
           ((total_pages - free_pages) * PAGE_SIZE) / (1024 * 1024));
    
    for (int i = 0; i < ZONE_COUNT; i++) {
        struct pmm_zone *zone = &zones[i];
        if (zone->total_pages > 0) {
            printk(KERN_INFO "  Zone %s: %lu total, %lu free\n",
                   zone->name, zone->total_pages, zone->free_pages);
        }
    }
    if (page_array) {
        printk(KERN_INFO "  struct page: %lu entries, backing phys=0x%lx\n",
               page_array_count, page_array_backing_phys);
    }
}

/* Initialize PMM (called from kernel main) */
void pmm_init(uint64_t multiboot_info) {
    printk(KERN_INFO "Initializing physical memory manager...\n");
    
    /* Parse multiboot memory map and set up bitmap */
    physmem_init(0x36d76289, (void *)multiboot_info);
    pmm_init_page_metadata();
    
    pmm_dump_stats();
}