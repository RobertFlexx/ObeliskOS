/*
 * Obelisk OS - Virtual Memory Manager
 * From Axioms, Order.
 *
 * Manages virtual address spaces and memory mappings.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/kmalloc.h>
#include <arch/mmu.h>
#include <proc/process.h>
#include <fs/vfs.h>

/* Kernel address space */
static struct address_space kernel_as;

/* VMA cache */
static struct kmem_cache *vma_cache;

/* ==========================================================================
 * Address Space Management
 * ========================================================================== */

/* Create a new address space */
struct address_space *vmm_create_address_space(void) {
    struct address_space *as = kzalloc(sizeof(struct address_space));
    if (!as) return NULL;
    
    /* Create page table */
    as->pt = mmu_create_page_table();
    if (!as->pt) {
        kfree(as);
        return NULL;
    }
    
    as->vma_list = NULL;
    as->vma_tree = RB_ROOT;
    as->vma_count = 0;
    as->lock = (spinlock_t)SPINLOCK_INIT;
    as->refcount.counter = 1;
    
    /* Set default heap location */
    as->brk_start = USER_HEAP_BASE;
    as->brk_end = USER_HEAP_BASE;
    
    return as;
}

/* Destroy an address space */
void vmm_destroy_address_space(struct address_space *as) {
    if (!as || as == &kernel_as) return;
    
    /* Free all VMAs */
    struct vm_area *vma = as->vma_list;
    while (vma) {
        struct vm_area *next = vma->next;
        vmm_free_vma(vma);
        vma = next;
    }
    
    /* Free page table */
    mmu_destroy_page_table(as->pt);
    
    kfree(as);
}

/* Clone an address space (for fork) */
struct address_space *vmm_clone_address_space(struct address_space *as) {
    struct address_space *new_as = vmm_create_address_space();
    if (!new_as) return NULL;
    
    /* Clone VMAs */
    struct vm_area *vma = as->vma_list;
    while (vma) {
        struct vm_area *new_vma = vmm_create_vma(vma->start, vma->end, vma->flags);
        if (!new_vma) {
            vmm_destroy_address_space(new_as);
            return NULL;
        }
        
        new_vma->prot = vma->prot;
        new_vma->file = vma->file;
        new_vma->offset = vma->offset;
        
        vmm_insert_vma(new_as, new_vma);
        
        /* Clone mapped pages eagerly.
         * We avoid COW here because the generic user write-fault
         * path is not fully wired for all regions yet. */
        for (uint64_t addr = vma->start; addr < vma->end; addr += PAGE_SIZE) {
            uint64_t phys = mmu_resolve(as->pt, addr);
            if (phys) {
                uint64_t flags = mmu_get_flags(as->pt, addr);

                uint64_t new_phys = pmm_alloc_pages(1);
                if (!new_phys) {
                    vmm_destroy_address_space(new_as);
                    return NULL;
                }
                memcpy(PHYS_TO_VIRT(new_phys), PHYS_TO_VIRT(phys), PAGE_SIZE);
                flags &= ~PTE_COW;
                mmu_map(new_as->pt, addr, new_phys, flags);
            }
        }
        
        vma = vma->next;
    }
    
    /* Copy heap boundaries */
    new_as->brk_start = as->brk_start;
    new_as->brk_end = as->brk_end;
    new_as->start_code = as->start_code;
    new_as->end_code = as->end_code;
    new_as->start_data = as->start_data;
    new_as->end_data = as->end_data;
    new_as->start_stack = as->start_stack;
    
    return new_as;
}

/* Get kernel address space */
struct address_space *vmm_get_kernel_address_space(void) {
    return &kernel_as;
}

/* ==========================================================================
 * VMA Operations
 * ========================================================================== */

/* Create a VMA */
struct vm_area *vmm_create_vma(uint64_t start, uint64_t end, uint32_t flags) {
    struct vm_area *vma;
    
    if (vma_cache) {
        vma = kmem_cache_alloc(vma_cache);
    } else {
        vma = kzalloc(sizeof(struct vm_area));
    }
    
    if (!vma) return NULL;
    
    vma->start = start;
    vma->end = end;
    vma->flags = flags;
    vma->prot = 0;
    vma->file = NULL;
    vma->offset = 0;
    vma->next = NULL;
    vma->prev = NULL;
    
    return vma;
}

/* Free a VMA */
void vmm_free_vma(struct vm_area *vma) {
    if (!vma) return;
    
    if (vma_cache) {
        kmem_cache_free(vma_cache, vma);
    } else {
        kfree(vma);
    }
}

/* Find VMA containing address */
struct vm_area *vmm_find_vma(struct address_space *as, uint64_t addr) {
    struct vm_area *vma = as->vma_list;
    
    while (vma) {
        if (addr >= vma->start && addr < vma->end) {
            return vma;
        }
        if (addr < vma->start) {
            break;
        }
        vma = vma->next;
    }
    
    return NULL;
}

/* Find VMA intersecting range */
struct vm_area *vmm_find_vma_intersection(struct address_space *as,
                                          uint64_t start, uint64_t end) {
    struct vm_area *vma = as->vma_list;
    
    while (vma) {
        if (vma->start < end && vma->end > start) {
            return vma;
        }
        if (vma->start >= end) {
            break;
        }
        vma = vma->next;
    }
    
    return NULL;
}

/* Insert VMA into address space (sorted by address) */
int vmm_insert_vma(struct address_space *as, struct vm_area *vma) {
    struct vm_area *prev = NULL;
    struct vm_area *curr = as->vma_list;
    
    /* Find insertion point */
    while (curr && curr->start < vma->start) {
        prev = curr;
        curr = curr->next;
    }
    
    /* Check for overlap */
    if (curr && curr->start < vma->end) {
        return -EEXIST;
    }
    if (prev && prev->end > vma->start) {
        return -EEXIST;
    }
    
    /* Insert */
    vma->prev = prev;
    vma->next = curr;
    
    if (prev) {
        prev->next = vma;
    } else {
        as->vma_list = vma;
    }
    
    if (curr) {
        curr->prev = vma;
    }
    
    as->vma_count++;
    as->total_vm += vma->end - vma->start;
    
    return 0;
}

/* Remove VMA from address space */
int vmm_remove_vma(struct address_space *as, struct vm_area *vma) {
    if (vma->prev) {
        vma->prev->next = vma->next;
    } else {
        as->vma_list = vma->next;
    }
    
    if (vma->next) {
        vma->next->prev = vma->prev;
    }
    
    as->vma_count--;
    as->total_vm -= vma->end - vma->start;
    
    return 0;
}

/* ==========================================================================
 * Memory Mapping
 * ========================================================================== */

/* Convert protection flags to PTE flags */
uint64_t vmm_prot_to_pte_flags(int prot) {
    uint64_t flags = PTE_PRESENT | PTE_USER;
    
    if (prot & PROT_WRITE) {
        flags |= PTE_WRITABLE;
    }
    /* NX is intentionally not set until EFER.NXE is enabled in CPU init. */
    (void)prot;

    return flags;
}

/* Map memory region */
void *vmm_mmap(struct address_space *as, void *addr, size_t length,
               int prot, int flags, struct file *file, off_t offset) {
    uint64_t start;
    uint64_t end;
    uint64_t pte_flags;

    if (!as || !as->pt || length == 0) {
        return MAP_FAILED;
    }
    if ((flags & MAP_ANONYMOUS) == 0) {
        if (!file || offset < 0 || ((uint64_t)offset & (PAGE_SIZE - 1)) != 0) {
            return MAP_FAILED;
        }
    }

    length = ALIGN_UP(length, PAGE_SIZE);
    
    if (flags & MAP_FIXED) {
        /* Fixed address requested */
        start = (uint64_t)addr;
        if ((start & (PAGE_SIZE - 1)) != 0) {
            return MAP_FAILED;
        }
        start = ALIGN_DOWN(start, PAGE_SIZE);
    } else {
        /* Find first suitable gap in user space. */
        start = USER_HEAP_BASE;
        while (1) {
            struct vm_area *vma;
            end = start + length;
            if (end < start || end > USER_SPACE_END) {
                return MAP_FAILED;
            }
            vma = vmm_find_vma_intersection(as, start, end);
            if (!vma) {
                break;
            }
            start = ALIGN_UP(vma->end, PAGE_SIZE);
        }
    }
    
    end = start + length;
    
    /* Check for overflow */
    if (end < start || end > USER_SPACE_END) {
        return MAP_FAILED;
    }
    
    /* Linux-like MAP_FIXED semantics: replace overlapping mappings. */
    if (flags & MAP_FIXED) {
        if (vmm_munmap(as, (void *)start, length) != 0) {
            return MAP_FAILED;
        }
    } else if (vmm_find_vma_intersection(as, start, end)) {
        return MAP_FAILED;
    }
    
    /* Create VMA */
    uint32_t vma_flags = 0;
    if (prot & PROT_READ) vma_flags |= VMA_READ;
    if (prot & PROT_WRITE) vma_flags |= VMA_WRITE;
    if (prot & PROT_EXEC) vma_flags |= VMA_EXEC;
    if (flags & MAP_SHARED) vma_flags |= VMA_SHARED;
    if (flags & MAP_PRIVATE) vma_flags |= VMA_PRIVATE;
    if (flags & MAP_ANONYMOUS) vma_flags |= VMA_ANONYMOUS;
    
    struct vm_area *vma = vmm_create_vma(start, end, vma_flags);
    if (!vma) {
        return MAP_FAILED;
    }
    
    vma->prot = prot;
    vma->file = file;
    vma->offset = offset;
    
    /* Insert VMA */
    if (vmm_insert_vma(as, vma) != 0) {
        vmm_free_vma(vma);
        return MAP_FAILED;
    }

    pte_flags = vmm_prot_to_pte_flags(prot);

    /* For file-backed mappings, eagerly populate page cache view:
     * - page-aligned file offsets
     * - zero-fill short reads / EOF tail
     * This matches dynamic-loader segment expectations better than pure demand-zero. */
    if ((flags & MAP_ANONYMOUS) == 0 && file) {
        for (uint64_t vaddr = start; vaddr < end; vaddr += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            off_t read_off = offset + (off_t)(vaddr - start);
            int ret;

            if (!phys) {
                vmm_munmap(as, (void *)start, length);
                return MAP_FAILED;
            }
            memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
            if (mmu_map(as->pt, vaddr, phys, pte_flags) != 0) {
                pmm_free_page(phys);
                vmm_munmap(as, (void *)start, length);
                return MAP_FAILED;
            }

            ret = vfs_read(file, PHYS_TO_VIRT(phys), PAGE_SIZE, &read_off);
            if (ret < 0) {
                vmm_munmap(as, (void *)start, length);
                return MAP_FAILED;
            }
            /* short read/EOF remains zero-filled */
        }
    }

    return (void *)start;
}

/* Unmap memory region */
int vmm_munmap(struct address_space *as, void *addr, size_t length) {
    uint64_t start = ALIGN_DOWN((uint64_t)addr, PAGE_SIZE);
    uint64_t end = ALIGN_UP((uint64_t)addr + length, PAGE_SIZE);
    
    /* Find and remove overlapping VMAs */
    struct vm_area *vma = as->vma_list;
    while (vma) {
        struct vm_area *next = vma->next;
        
        if (vma->start >= end) {
            break;
        }
        
        if (vma->end > start) {
            /* VMA overlaps with unmapped region */
            
            /* Unmap pages */
            uint64_t unmap_start = MAX(vma->start, start);
            uint64_t unmap_end = MIN(vma->end, end);
            
            for (uint64_t addr = unmap_start; addr < unmap_end; addr += PAGE_SIZE) {
                uint64_t phys = mmu_resolve(as->pt, addr);
                if (phys) {
                    mmu_unmap(as->pt, addr);
                    pmm_free_page(phys);
                }
            }
            
            if (vma->start >= start && vma->end <= end) {
                /* Entire VMA is unmapped */
                vmm_remove_vma(as, vma);
                vmm_free_vma(vma);
            } else if (vma->start < start && vma->end > end) {
                /* VMA is split */
                struct vm_area *new_vma = vmm_create_vma(end, vma->end, vma->flags);
                if (new_vma) {
                    new_vma->prot = vma->prot;
                    new_vma->file = vma->file;
                    new_vma->offset = vma->offset + (end - vma->start);
                    vma->end = start;
                    vmm_insert_vma(as, new_vma);
                }
            } else if (vma->start < start) {
                /* Trim end */
                vma->end = start;
            } else {
                /* Trim start */
                vma->offset += end - vma->start;
                vma->start = end;
            }
        }
        
        vma = next;
    }
    
    return 0;
}

/* Change memory protection */
int vmm_mprotect(struct address_space *as, void *addr, size_t length, int prot) {
    uint64_t start = ALIGN_DOWN((uint64_t)addr, PAGE_SIZE);
    uint64_t end = ALIGN_UP((uint64_t)addr + length, PAGE_SIZE);
    
    uint64_t flags = vmm_prot_to_pte_flags(prot);
    
    for (uint64_t a = start; a < end; a += PAGE_SIZE) {
        if (mmu_resolve(as->pt, a)) {
            mmu_set_flags(as->pt, a, flags);
        }
    }
    
    return 0;
}

/* ==========================================================================
 * Heap Management
 * ========================================================================== */

/* Change program break */
void *vmm_brk(struct address_space *as, void *addr) {
    uint64_t new_brk = (uint64_t)addr;
    
    if (new_brk == 0) {
        return (void *)as->brk_end;
    }
    
    new_brk = ALIGN_UP(new_brk, PAGE_SIZE);
    
    if (new_brk < as->brk_start) {
        return (void *)-1;
    }
    
    if (new_brk > as->brk_end) {
        /* Expand heap */
        for (uint64_t addr = as->brk_end; addr < new_brk; addr += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                return (void *)-1;
            }
            
            int ret = mmu_map(as->pt, addr, phys,
                             PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);
            if (ret != 0) {
                pmm_free_page(phys);
                return (void *)-1;
            }
            
            memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        }
    } else if (new_brk < as->brk_end) {
        /* Shrink heap */
        for (uint64_t addr = new_brk; addr < as->brk_end; addr += PAGE_SIZE) {
            uint64_t phys = mmu_resolve(as->pt, addr);
            if (phys) {
                mmu_unmap(as->pt, addr);
                pmm_free_page(phys);
            }
        }
    }
    
    as->brk_end = new_brk;
    return (void *)new_brk;
}

/* ==========================================================================
 * Page Fault Handling
 * ========================================================================== */

/* Handle page fault */
int vmm_handle_page_fault(struct address_space *as, uint64_t addr,
                          uint32_t error_code) {
    /* Find VMA */
    struct vm_area *vma = vmm_find_vma(as, addr);
    if (!vma) {
        /* Address not mapped - segfault */
        return -EFAULT;
    }
    
    /* Check permissions */
    if ((error_code & 0x2) && !(vma->flags & VMA_WRITE)) {
        /* Write to read-only page */
        return -EACCES;
    }
    
    /* Check for copy-on-write */
    uint64_t page_addr = ALIGN_DOWN(addr, PAGE_SIZE);
    uint64_t old_phys = mmu_resolve(as->pt, page_addr);
    
    if (old_phys && (mmu_get_flags(as->pt, page_addr) & PTE_COW)) {
        /* Copy-on-write fault */
        uint64_t new_phys = pmm_alloc_page();
        if (!new_phys) {
            return -ENOMEM;
        }
        
        /* Copy page contents */
        memcpy(PHYS_TO_VIRT(new_phys), PHYS_TO_VIRT(old_phys), PAGE_SIZE);
        
        /* Remap with write permission */
        uint64_t flags = vmm_prot_to_pte_flags(vma->prot);
        mmu_unmap(as->pt, page_addr);
        mmu_map(as->pt, page_addr, new_phys, flags);
        
        return 0;
    }
    
    /* Demand paging - allocate new page */
    uint64_t phys = pmm_alloc_page();
    if (!phys) {
        return -ENOMEM;
    }
    
    /* Zero the page */
    memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
    
    /* Map the page */
    uint64_t flags = vmm_prot_to_pte_flags(vma->prot);
    int ret = mmu_map(as->pt, page_addr, phys, flags);
    if (ret != 0) {
        pmm_free_page(phys);
        return ret;
    }
    
    return 0;
}

/* ==========================================================================
 * Kernel Memory
 * ========================================================================== */

/* Allocate kernel pages */
void *vmm_alloc_kernel_pages(size_t count) {
    uint64_t phys = pmm_alloc_pages(count);
    if (!phys) {
        return NULL;
    }
    
    return PHYS_TO_VIRT(phys);
}

/* Free kernel pages */
void vmm_free_kernel_pages(void *addr, size_t count) {
    uint64_t phys = VIRT_TO_PHYS((uint64_t)addr);
    pmm_free_pages(phys, count);
}

/* Map physical memory into kernel space */
void *vmm_map_physical(uint64_t phys, size_t size, uint64_t flags) {
    /* In higher-half kernel, physical memory is directly mapped */
    (void)size;
    (void)flags;
    return PHYS_TO_VIRT(phys);
}

/* Unmap physical memory */
void vmm_unmap_physical(void *virt, size_t size) {
    /* No-op for direct mapping */
    (void)virt;
    (void)size;
}

/* ==========================================================================
 * User Memory Access
 * ========================================================================== */

static int vmm_verify_user_range(const void *addr, size_t size, bool write) {
    struct process *proc = current;
    struct address_space *as;
    uintptr_t start;
    uintptr_t end;
    uintptr_t page;

    if (!addr) {
        return -EFAULT;
    }
    if (size == 0) {
        return 0;
    }

    start = (uintptr_t)addr;
    if (start >= USER_SPACE_END) {
        return -EFAULT;
    }
    if (size > (USER_SPACE_END - start)) {
        return -EFAULT;
    }

    if (!proc || !proc->mm || !proc->mm->pt) {
        return -EFAULT;
    }
    as = proc->mm;
    end = start + size;

    for (page = ALIGN_DOWN(start, PAGE_SIZE); page < end; page += PAGE_SIZE) {
        uint64_t phys = mmu_resolve(as->pt, page);
        uint64_t flags;
        if (!phys) {
            return -EFAULT;
        }
        flags = mmu_get_flags(as->pt, page);
        if (!(flags & PTE_PRESENT) || !(flags & PTE_USER)) {
            return -EFAULT;
        }
        if (write && !(flags & PTE_WRITABLE)) {
            return -EFAULT;
        }
    }

    return 0;
}

/* Copy from user space */
int vmm_copy_from_user(void *dst, const void *src, size_t size) {
    int ret;
    if (!dst) {
        return -EFAULT;
    }
    ret = vmm_verify_user_range(src, size, false);
    if (ret < 0) {
        return ret;
    }
    memcpy(dst, src, size);
    return 0;
}

/* Copy to user space */
int vmm_copy_to_user(void *dst, const void *src, size_t size) {
    int ret;
    if (!src) {
        return -EFAULT;
    }
    ret = vmm_verify_user_range(dst, size, true);
    if (ret < 0) {
        return ret;
    }
    memcpy(dst, src, size);
    return 0;
}

/* Verify user read access */
int vmm_verify_user_read(const void *addr, size_t size) {
    return vmm_verify_user_range(addr, size, false);
}

/* Verify user write access */
int vmm_verify_user_write(void *addr, size_t size) {
    return vmm_verify_user_range(addr, size, true);
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void vmm_init(void) {
    printk(KERN_INFO "Initializing virtual memory manager...\n");
    
    /* Initialize paging */
    extern void paging_init(void);
    paging_init();
    
    /* Set up kernel address space */
    kernel_as.pt = mmu_get_kernel_pt();
    kernel_as.vma_list = NULL;
    kernel_as.vma_tree = RB_ROOT;
    kernel_as.vma_count = 0;
    kernel_as.lock = (spinlock_t)SPINLOCK_INIT;
    kernel_as.refcount.counter = 1;
    
    /* Create VMA cache (after kmalloc is available) */
    vma_cache = NULL;  /* Will be initialized later */
    
    printk(KERN_INFO "VMM initialized\n");
}