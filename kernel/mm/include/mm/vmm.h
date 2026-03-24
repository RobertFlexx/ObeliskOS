/*
 * Obelisk OS - Virtual Memory Manager Header
 * From Axioms, Order.
 */

#ifndef _MM_VMM_H
#define _MM_VMM_H

#include <obelisk/types.h>
#include <arch/mmu.h>

/* mmap/PROT compatibility flags */
#ifndef PROT_READ
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#endif

#ifndef MAP_SHARED
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_GROWSDOWN   0x0100
#define MAP_FAILED      ((void *)-1)
#endif

/* Virtual memory area flags */
#define VMA_READ        BIT(0)
#define VMA_WRITE       BIT(1)
#define VMA_EXEC        BIT(2)
#define VMA_SHARED      BIT(3)
#define VMA_PRIVATE     BIT(4)
#define VMA_GROWSDOWN   BIT(5)  /* Stack */
#define VMA_GROWSUP     BIT(6)  /* Heap */
#define VMA_ANONYMOUS   BIT(7)
#define VMA_FILE        BIT(8)
#define VMA_FIXED       BIT(9)
#define VMA_LOCKED      BIT(10)
#define VMA_DONTCOPY    BIT(11)
#define VMA_DONTEXPAND  BIT(12)

/* Virtual memory area structure */
struct vm_area {
    uint64_t start;             /* Start address */
    uint64_t end;               /* End address */
    uint32_t flags;             /* VMA flags */
    uint32_t prot;              /* Protection flags */
    struct file *file;          /* Backing file (if any) */
    uint64_t offset;            /* File offset */
    struct vm_area *next;       /* Next VMA in list */
    struct vm_area *prev;       /* Previous VMA in list */
    struct rb_node rb_node;     /* Red-black tree node */
};

/* Address space structure */
struct address_space {
    struct page_table *pt;      /* Page table */
    struct vm_area *vma_list;   /* VMA list head */
    struct rb_root vma_tree;    /* VMA red-black tree */
    size_t vma_count;           /* Number of VMAs */
    uint64_t total_vm;          /* Total virtual memory */
    uint64_t locked_vm;         /* Locked virtual memory */
    uint64_t data_vm;           /* Data segment size */
    uint64_t stack_vm;          /* Stack size */
    uint64_t exec_vm;           /* Executable size */
    uint64_t brk_start;         /* Heap start */
    uint64_t brk_end;           /* Current heap end */
    uint64_t start_code;        /* Code segment start */
    uint64_t end_code;          /* Code segment end */
    uint64_t start_data;        /* Data segment start */
    uint64_t end_data;          /* Data segment end */
    uint64_t start_stack;       /* Stack start */
    spinlock_t lock;            /* Address space lock */
    atomic_t refcount;          /* Reference count */
};

/* Initialization */
void vmm_init(void);

/* Address space management */
struct address_space *vmm_create_address_space(void);
void vmm_destroy_address_space(struct address_space *as);
struct address_space *vmm_clone_address_space(struct address_space *as);
struct address_space *vmm_get_kernel_address_space(void);

/* VMA operations */
struct vm_area *vmm_find_vma(struct address_space *as, uint64_t addr);
struct vm_area *vmm_find_vma_intersection(struct address_space *as,
                                          uint64_t start, uint64_t end);
int vmm_insert_vma(struct address_space *as, struct vm_area *vma);
int vmm_remove_vma(struct address_space *as, struct vm_area *vma);
struct vm_area *vmm_create_vma(uint64_t start, uint64_t end, uint32_t flags);
void vmm_free_vma(struct vm_area *vma);

/* Memory mapping */
void *vmm_mmap(struct address_space *as, void *addr, size_t length,
               int prot, int flags, struct file *file, off_t offset);
int vmm_munmap(struct address_space *as, void *addr, size_t length);
int vmm_mprotect(struct address_space *as, void *addr, size_t length, int prot);
int vmm_mremap(struct address_space *as, void *old_addr, size_t old_size,
               size_t new_size, int flags, void **new_addr);

/* Heap management */
void *vmm_brk(struct address_space *as, void *addr);

/* Page fault handling */
int vmm_handle_page_fault(struct address_space *as, uint64_t addr,
                          uint32_t error_code);

/* Kernel virtual memory */
void *vmm_alloc_kernel_pages(size_t count);
void vmm_free_kernel_pages(void *addr, size_t count);
void *vmm_map_physical(uint64_t phys, size_t size, uint64_t flags);
void vmm_unmap_physical(void *virt, size_t size);

/* Utility functions */
int vmm_copy_from_user(void *dst, const void *src, size_t size);
int vmm_copy_to_user(void *dst, const void *src, size_t size);
int vmm_verify_user_read(const void *addr, size_t size);
int vmm_verify_user_write(void *addr, size_t size);

/* Convert protection flags to page table flags */
uint64_t vmm_prot_to_pte_flags(int prot);

#endif /* _MM_VMM_H */