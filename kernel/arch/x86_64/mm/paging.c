/*
 * Obelisk OS - Page Table Management
 * From Axioms, Order.
 *
 * Implements x86_64 4-level paging.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

/* Kernel page table (set up during boot) */
static struct page_table kernel_pt;

/* ==========================================================================
 * Low-level page table operations
 * ========================================================================== */

/* Get page table entry at a given level */
static uint64_t *paging_get_entry(uint64_t *table, uint64_t virt, int level) {
    uint64_t index;
    
    switch (level) {
        case 4: index = PML4_INDEX(virt); break;
        case 3: index = PDPT_INDEX(virt); break;
        case 2: index = PD_INDEX(virt); break;
        case 1: index = PT_INDEX(virt); break;
        default: return NULL;
    }
    
    return &table[index];
}

/* Get or create the next level page table */
static uint64_t *paging_get_or_create(uint64_t *entry, uint64_t flags) {
    if (*entry & PTE_PRESENT) {
        /* Entry exists, return it */
        uint64_t phys = pte_get_frame(*entry);
        return (uint64_t *)PHYS_TO_VIRT(phys);
    }
    
    /* Allocate a new page table */
    uint64_t new_table = pmm_alloc_page();
    if (new_table == 0) {
        return NULL;
    }
    
    /* Zero the new table */
    void *virt = PHYS_TO_VIRT(new_table);
    memset(virt, 0, PAGE_SIZE);
    
    /* Set the entry */
    *entry = pte_make(new_table, flags | PTE_PRESENT);
    
    return (uint64_t *)virt;
}

/* Walk page tables to find the PTE for a virtual address */
static uint64_t *paging_walk(struct page_table *pt, uint64_t virt, bool create) {
    uint64_t *pml4 = pt->pml4;
    uint64_t *pdpt, *pd, *page_table;
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    
    /* Check if user address */
    if (virt < KERNEL_VIRT_BASE) {
        flags |= PTE_USER;
    }
    
    /* Level 4: PML4 */
    uint64_t *pml4e = paging_get_entry(pml4, virt, 4);
    if (!(*pml4e & PTE_PRESENT)) {
        if (!create) return NULL;
        pdpt = paging_get_or_create(pml4e, flags);
        if (!pdpt) return NULL;
    } else {
        pdpt = (uint64_t *)PHYS_TO_VIRT(pte_get_frame(*pml4e));
    }
    
    /* Level 3: PDPT */
    uint64_t *pdpte = paging_get_entry(pdpt, virt, 3);
    if (*pdpte & PTE_HUGE) {
        /* 1GB page */
        return pdpte;
    }
    if (!(*pdpte & PTE_PRESENT)) {
        if (!create) return NULL;
        pd = paging_get_or_create(pdpte, flags);
        if (!pd) return NULL;
    } else {
        pd = (uint64_t *)PHYS_TO_VIRT(pte_get_frame(*pdpte));
    }
    
    /* Level 2: PD */
    uint64_t *pde = paging_get_entry(pd, virt, 2);
    if (*pde & PTE_HUGE) {
        /* 2MB page */
        return pde;
    }
    if (!(*pde & PTE_PRESENT)) {
        if (!create) return NULL;
        page_table = paging_get_or_create(pde, flags);
        if (!page_table) return NULL;
    } else {
        page_table = (uint64_t *)PHYS_TO_VIRT(pte_get_frame(*pde));
    }
    
    /* Level 1: PT */
    return paging_get_entry(page_table, virt, 1);
}

/* ==========================================================================
 * Page table management API
 * ========================================================================== */

/* Create a new page table */
struct page_table *mmu_create_page_table(void) {
    struct page_table *pt = kmalloc(sizeof(struct page_table));
    if (!pt) return NULL;
    
    /* Allocate PML4 */
    uint64_t pml4_phys = pmm_alloc_page();
    if (pml4_phys == 0) {
        kfree(pt);
        return NULL;
    }
    
    pt->pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);
    pt->pml4_phys = pml4_phys;
    pt->lock = (spinlock_t)SPINLOCK_INIT;
    pt->refcount.counter = 1;
    
    /* Zero the PML4 */
    memset(pt->pml4, 0, PAGE_SIZE);
    
    /* Copy kernel mappings (higher half entries) */
    struct page_table *kernel = mmu_get_kernel_pt();
    for (int i = 256; i < 512; i++) {
        pt->pml4[i] = kernel->pml4[i];
    }
    
    return pt;
}

/* Destroy a page table */
void mmu_destroy_page_table(struct page_table *pt) {
    if (!pt || pt == &kernel_pt) return;
    
    /* TODO: Walk and free all user-space page tables */
    /* For now, just free the PML4 */
    pmm_free_page(pt->pml4_phys);
    kfree(pt);
}

/* Map a virtual address to a physical address */
int mmu_map(struct page_table *pt, uint64_t virt, uint64_t phys, uint64_t flags) {
    /* Ensure page alignment */
    virt = ALIGN_DOWN(virt, PAGE_SIZE);
    phys = ALIGN_DOWN(phys, PAGE_SIZE);
    
    /* Get or create the PTE */
    uint64_t *pte = paging_walk(pt, virt, true);
    if (!pte) {
        return -ENOMEM;
    }
    
    /* Check if already mapped */
    if (*pte & PTE_PRESENT) {
        return -EEXIST;
    }
    
    /* Set the mapping */
    *pte = pte_make(phys, flags | PTE_PRESENT);
    
    /* Invalidate TLB for this page */
    if (pt == &kernel_pt || pt->pml4_phys == read_cr3()) {
        invlpg((void *)virt);
    }
    
    return 0;
}

/* Unmap a virtual address */
int mmu_unmap(struct page_table *pt, uint64_t virt) {
    virt = ALIGN_DOWN(virt, PAGE_SIZE);
    
    uint64_t *pte = paging_walk(pt, virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return -ENOENT;
    }
    
    /* Clear the entry */
    *pte = 0;
    
    /* Invalidate TLB */
    if (pt == &kernel_pt || pt->pml4_phys == read_cr3()) {
        invlpg((void *)virt);
    }
    
    return 0;
}

/* Map a range of pages */
int mmu_map_range(struct page_table *pt, uint64_t virt, uint64_t phys,
                  size_t size, uint64_t flags) {
    size = ALIGN_UP(size, PAGE_SIZE);
    
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        int ret = mmu_map(pt, virt + offset, phys + offset, flags);
        if (ret != 0 && ret != -EEXIST) {
            /* Rollback on failure */
            mmu_unmap_range(pt, virt, offset);
            return ret;
        }
    }
    
    return 0;
}

/* Unmap a range of pages */
void mmu_unmap_range(struct page_table *pt, uint64_t virt, size_t size) {
    size = ALIGN_UP(size, PAGE_SIZE);
    
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        mmu_unmap(pt, virt + offset);
    }
}

/* Resolve virtual address to physical address */
uint64_t mmu_resolve(struct page_table *pt, uint64_t virt) {
    uint64_t *pte = paging_walk(pt, virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return 0;
    }
    
    uint64_t offset = virt & PAGE_OFFSET_MASK;
    return pte_get_frame(*pte) | offset;
}

/* Get flags for a virtual address */
uint64_t mmu_get_flags(struct page_table *pt, uint64_t virt) {
    uint64_t *pte = paging_walk(pt, virt, false);
    if (!pte) {
        return 0;
    }
    return *pte & ~PAGE_FRAME_MASK;
}

/* Set flags for a virtual address */
int mmu_set_flags(struct page_table *pt, uint64_t virt, uint64_t flags) {
    uint64_t *pte = paging_walk(pt, virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return -ENOENT;
    }
    
    uint64_t phys = pte_get_frame(*pte);
    *pte = pte_make(phys, flags | PTE_PRESENT);
    
    if (pt == &kernel_pt || pt->pml4_phys == read_cr3()) {
        invlpg((void *)virt);
    }
    
    return 0;
}

/* Switch to a different page table */
void mmu_switch(struct page_table *pt) {
    write_cr3(pt->pml4_phys);
}

/* Get kernel page table */
struct page_table *mmu_get_kernel_pt(void) {
    return &kernel_pt;
}

/* Flush TLB for a single page */
void mmu_flush_tlb_page(uint64_t addr) {
    invlpg((void *)addr);
}

/* Flush TLB for a range of pages */
void mmu_flush_tlb_range(uint64_t start, uint64_t end) {
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        invlpg((void *)addr);
    }
}

/* Flush entire TLB */
void mmu_flush_tlb_all(void) {
    write_cr3(read_cr3());
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

/* Initialize paging subsystem */
void paging_init(void) {
    /* Set up kernel page table structure */
    kernel_pt.pml4_phys = read_cr3();
    kernel_pt.pml4 = (uint64_t *)PHYS_TO_VIRT(kernel_pt.pml4_phys);
    kernel_pt.lock = (spinlock_t)SPINLOCK_INIT;
    kernel_pt.refcount.counter = 1;
    
    printk(KERN_INFO "Paging initialized, kernel PML4 at 0x%lx\n", 
           kernel_pt.pml4_phys);
}