/*
 * Obelisk OS - Physical Memory Manager Header
 * From Axioms, Order.
 */

#ifndef _MM_PMM_H
#define _MM_PMM_H

#include <obelisk/types.h>

/* Memory zone types */
typedef enum {
    ZONE_DMA,       /* 0-16MB: DMA-capable memory */
    ZONE_NORMAL,    /* 16MB-4GB: Normal memory */
    ZONE_HIGH,      /* >4GB: High memory */
    ZONE_COUNT
} zone_type_t;

/* Memory zone structure */
struct pmm_zone {
    const char *name;           /* Zone name */
    uint64_t start_pfn;         /* Start page frame number */
    uint64_t end_pfn;           /* End page frame number */
    uint64_t total_pages;       /* Total pages in zone */
    uint64_t free_pages;        /* Free pages in zone */
    uint32_t *bitmap;           /* Allocation bitmap */
    size_t bitmap_size;         /* Bitmap size in bytes */
    spinlock_t lock;            /* Zone lock */
};

/* Page flags */
#define PAGE_FLAG_RESERVED      BIT(0)
#define PAGE_FLAG_KERNEL        BIT(1)
#define PAGE_FLAG_USER          BIT(2)
#define PAGE_FLAG_DMA           BIT(3)
#define PAGE_FLAG_LOCKED        BIT(4)
#define PAGE_FLAG_DIRTY         BIT(5)
#define PAGE_FLAG_REFERENCED    BIT(6)
#define PAGE_FLAG_SLAB          BIT(7)

/* Page structure (for future page tracking) */
struct page {
    uint32_t flags;             /* Page flags */
    uint32_t refcount;          /* Reference count */
    uint16_t zone;              /* zone_type_t */
    uint16_t owner;             /* Ownership category */
    struct list_head list;      /* Free list or LRU list */
    void *private;              /* Private data (e.g., slab pointer) */
};

enum page_owner {
    PAGE_OWNER_FREE = 0,
    PAGE_OWNER_KERNEL = 1,
    PAGE_OWNER_USER = 2,
    PAGE_OWNER_SLAB = 3,
    PAGE_OWNER_PAGETABLE = 4,
    PAGE_OWNER_DMA = 5,
};

/* Initialization */
void pmm_init(uint64_t multiboot_info);
void pmm_init_bitmap(uint64_t total_pages);

/* Region management */
void pmm_mark_region_free(uint64_t base, uint64_t size);
void pmm_mark_region_used(uint64_t base, uint64_t size);

/* Page allocation */
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(size_t count);
uint64_t pmm_alloc_page_zone(zone_type_t zone);
uint64_t pmm_alloc_pages_zone(size_t count, zone_type_t zone);

/* Page deallocation */
void pmm_free_page(uint64_t phys);
void pmm_free_pages(uint64_t phys, size_t count);

/* Statistics */
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_usable_pages(void);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_used_pages(void);
void pmm_dump_stats(void);

/* Zone management */
struct pmm_zone *pmm_get_zone(zone_type_t type);

/* Live per-page metadata accessors */
struct page *pmm_get_page_by_pfn(uint64_t pfn);
struct page *pmm_get_page_by_phys(uint64_t phys);
void pmm_page_set_owner(uint64_t phys, uint16_t owner);
void pmm_page_update_flags(uint64_t phys, uint32_t set_flags, uint32_t clear_flags);
void pmm_page_ref_inc(uint64_t phys);
bool pmm_page_ref_dec(uint64_t phys);

/* Page frame operations */
static inline uint64_t pfn_to_phys(uint64_t pfn) {
    return pfn << PAGE_SHIFT;
}

static inline uint64_t phys_to_pfn(uint64_t phys) {
    return phys >> PAGE_SHIFT;
}

#endif /* _MM_PMM_H */
