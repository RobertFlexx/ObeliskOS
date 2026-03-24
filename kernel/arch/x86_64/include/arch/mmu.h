/*
 * Obelisk OS - x86_64 MMU Definitions
 * From Axioms, Order.
 */

#ifndef _ARCH_MMU_H
#define _ARCH_MMU_H

#include <obelisk/types.h>

/* Page table entry flags */
#define PTE_PRESENT         BIT(0)      /* Page present */
#define PTE_WRITABLE        BIT(1)      /* Read/write */
#define PTE_USER            BIT(2)      /* User/supervisor */
#define PTE_WRITETHROUGH    BIT(3)      /* Write-through */
#define PTE_NOCACHE         BIT(4)      /* Cache disable */
#define PTE_ACCESSED        BIT(5)      /* Accessed */
#define PTE_DIRTY           BIT(6)      /* Dirty */
#define PTE_HUGE            BIT(7)      /* Huge page (PS) */
#define PTE_GLOBAL          BIT(8)      /* Global */
#define PTE_NX              BIT(63)     /* No execute */

/* Custom flags in available bits */
#define PTE_COW             BIT(9)      /* Copy-on-write */
#define PTE_ALLOCATED       BIT(10)     /* Allocated but not mapped */

/* Page table constants */
#define PT_ENTRIES          512
#define PT_LEVELS           4

/* Page sizes */
#define PAGE_SIZE_4K        0x1000UL
#define PAGE_SIZE_2M        0x200000UL
#define PAGE_SIZE_1G        0x40000000UL

/* Address space layout */
#define PML4_SHIFT          39
#define PDPT_SHIFT          30
#define PD_SHIFT            21
#define PT_SHIFT            12

#define PML4_INDEX(addr)    (((addr) >> PML4_SHIFT) & 0x1FF)
#define PDPT_INDEX(addr)    (((addr) >> PDPT_SHIFT) & 0x1FF)
#define PD_INDEX(addr)      (((addr) >> PD_SHIFT) & 0x1FF)
#define PT_INDEX(addr)      (((addr) >> PT_SHIFT) & 0x1FF)

/* Address masks */
#define PAGE_FRAME_MASK     0x000FFFFFFFFFF000UL
#define PAGE_OFFSET_MASK    0xFFFUL

/* Page table entry manipulation */
static __always_inline uint64_t pte_get_frame(uint64_t pte) {
    return pte & PAGE_FRAME_MASK;
}

static __always_inline uint64_t pte_make(uint64_t frame, uint64_t flags) {
    return (frame & PAGE_FRAME_MASK) | flags;
}

static __always_inline bool pte_is_present(uint64_t pte) {
    return (pte & PTE_PRESENT) != 0;
}

static __always_inline bool pte_is_writable(uint64_t pte) {
    return (pte & PTE_WRITABLE) != 0;
}

static __always_inline bool pte_is_user(uint64_t pte) {
    return (pte & PTE_USER) != 0;
}

static __always_inline bool pte_is_huge(uint64_t pte) {
    return (pte & PTE_HUGE) != 0;
}

static __always_inline bool pte_is_executable(uint64_t pte) {
    return (pte & PTE_NX) == 0;
}

/* Page table structure */
struct page_table {
    uint64_t *pml4;         /* Virtual address of PML4 */
    uint64_t pml4_phys;     /* Physical address of PML4 */
    spinlock_t lock;        /* Page table lock */
    atomic_t refcount;      /* Reference count */
};

/* Page table operations */
struct page_table *mmu_create_page_table(void);
void mmu_destroy_page_table(struct page_table *pt);
struct page_table *mmu_clone_page_table(struct page_table *pt);

int mmu_map(struct page_table *pt, uint64_t virt, uint64_t phys, uint64_t flags);
int mmu_unmap(struct page_table *pt, uint64_t virt);
int mmu_map_range(struct page_table *pt, uint64_t virt, uint64_t phys,
                  size_t size, uint64_t flags);
void mmu_unmap_range(struct page_table *pt, uint64_t virt, size_t size);

uint64_t mmu_resolve(struct page_table *pt, uint64_t virt);
uint64_t mmu_get_flags(struct page_table *pt, uint64_t virt);
int mmu_set_flags(struct page_table *pt, uint64_t virt, uint64_t flags);

void mmu_switch(struct page_table *pt);
struct page_table *mmu_get_kernel_pt(void);

/* TLB operations */
void mmu_flush_tlb_page(uint64_t addr);
void mmu_flush_tlb_range(uint64_t start, uint64_t end);
void mmu_flush_tlb_all(void);

#endif /* _ARCH_MMU_H */