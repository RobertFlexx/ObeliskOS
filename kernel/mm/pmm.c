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

/* Initialize PMM bitmap */
void pmm_init_bitmap(uint64_t num_pages) {
    total_pages = num_pages;
    
    /* Calculate bitmap size (1 bit per page) */
    size_t bitmap_pages = ALIGN_UP(num_pages / 8, PAGE_SIZE) / PAGE_SIZE;
    (void)bitmap_pages;
    
    /* Update zone boundaries */
    if (num_pages > 0x100000) {
        zones[ZONE_HIGH].end_pfn = num_pages;
    } else if (num_pages > 0x1000) {
        zones[ZONE_NORMAL].end_pfn = num_pages;
        zones[ZONE_HIGH].end_pfn = zones[ZONE_HIGH].start_pfn;
    } else {
        zones[ZONE_DMA].end_pfn = num_pages;
        zones[ZONE_NORMAL].end_pfn = zones[ZONE_NORMAL].start_pfn;
        zones[ZONE_HIGH].end_pfn = zones[ZONE_HIGH].start_pfn;
    }
    
    /* For initial setup, we use a static area in BSS */
    /* This will be replaced with proper memory once we can allocate */
    static uint32_t initial_bitmap[1024 * 1024 / 32]; /* 1M pages = 4GB */
    
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
}

/* Initialize PMM (called from kernel main) */
void pmm_init(uint64_t multiboot_info) {
    printk(KERN_INFO "Initializing physical memory manager...\n");
    
    /* Parse multiboot memory map and set up bitmap */
    physmem_init(0x36d76289, (void *)multiboot_info);
    
    pmm_dump_stats();
}