/*
 * Obelisk OS - Shared Memory Implementation
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <uapi/syscall.h>
#include <mm/kmalloc.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <proc/process.h>

/* Shared memory segment */
struct shm_segment {
    uint32_t id;
    uint32_t key;
    uid_t uid;
    gid_t gid;
    mode_t mode;
    size_t size;
    size_t nattach;
    uint64_t *pages;
    size_t num_pages;
    uint64_t atime;
    uint64_t dtime;
    uint64_t ctime;
    spinlock_t lock;
    struct list_head list;
};

/* Shared memory table */
#define MAX_SHM_SEGMENTS    256
static struct shm_segment *shm_table[MAX_SHM_SEGMENTS];
static uint32_t next_shm_id = 1;

/* ==========================================================================
 * Shared Memory Operations
 * ========================================================================== */

#ifndef SHM_RDONLY
#define SHM_RDONLY  0x1000
#endif

int shmget(uint32_t key, size_t size, int flags) {
    struct shm_segment *seg;
    int id;
    
    /* Check for existing segment */
    if (key != 0) {
        for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
            if (shm_table[i] && shm_table[i]->key == key) {
                if (flags & O_EXCL) {
                    return -EEXIST;
                }
                return i;
            }
        }
    }
    
    if (!(flags & O_CREAT)) {
        return -ENOENT;
    }
    
    /* Find free slot */
    id = -1;
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_table[i] == NULL) {
            id = i;
            break;
        }
    }
    
    if (id < 0) {
        return -ENOSPC;
    }
    
    /* Allocate segment */
    seg = kzalloc(sizeof(struct shm_segment));
    if (!seg) {
        return -ENOMEM;
    }
    
    seg->id = next_shm_id++;
    seg->key = key;
    seg->uid = current ? current->cred->uid : 0;
    seg->gid = current ? current->cred->gid : 0;
    seg->mode = flags & 0777;
    seg->size = ALIGN_UP(size, PAGE_SIZE);
    seg->nattach = 0;
    seg->num_pages = seg->size / PAGE_SIZE;
    seg->lock = (spinlock_t)SPINLOCK_INIT;
    seg->ctime = get_ticks();
    
    /* Allocate page array */
    seg->pages = kzalloc(seg->num_pages * sizeof(uint64_t));
    if (!seg->pages) {
        kfree(seg);
        return -ENOMEM;
    }
    
    /* Allocate physical pages */
    for (size_t i = 0; i < seg->num_pages; i++) {
        seg->pages[i] = pmm_alloc_page();
        if (!seg->pages[i]) {
            /* Free already allocated pages */
            for (size_t j = 0; j < i; j++) {
                pmm_free_page(seg->pages[j]);
            }
            kfree(seg->pages);
            kfree(seg);
            return -ENOMEM;
        }
        /* Zero the page */
        memset(PHYS_TO_VIRT(seg->pages[i]), 0, PAGE_SIZE);
    }
    
    shm_table[id] = seg;
    
    return id;
}

void *shmat(int shmid, const void *addr, int flags) {
    struct shm_segment *seg;
    struct process *proc = current;
    uint64_t vaddr;
    
    if (shmid < 0 || shmid >= MAX_SHM_SEGMENTS) {
        return (void *)-EINVAL;
    }
    
    seg = shm_table[shmid];
    if (!seg) {
        return (void *)-EINVAL;
    }
    
    if (!proc || !proc->mm) {
        return (void *)-EINVAL;
    }
    
    /* Find virtual address */
    if (addr) {
        vaddr = (uint64_t)addr;
    } else {
        /* Find free region */
        vaddr = 0x40000000;  /* Start looking here */
        while (vmm_find_vma(proc->mm, vaddr) != NULL) {
            vaddr += seg->size;
            if (vaddr >= USER_SPACE_END) {
                return (void *)-ENOMEM;
            }
        }
    }
    
    /* Map pages */
    uint64_t pte_flags = PTE_PRESENT | PTE_USER;
    if (!(flags & SHM_RDONLY)) {
        pte_flags |= PTE_WRITABLE;
    }
    
    for (size_t i = 0; i < seg->num_pages; i++) {
        int ret = mmu_map(proc->mm->pt, vaddr + i * PAGE_SIZE,
                          seg->pages[i], pte_flags);
        if (ret < 0) {
            /* Unmap already mapped pages */
            for (size_t j = 0; j < i; j++) {
                mmu_unmap(proc->mm->pt, vaddr + j * PAGE_SIZE);
            }
            return (void *)(long)ret;
        }
    }
    
    seg->nattach++;
    seg->atime = get_ticks();
    
    return (void *)vaddr;
}

int shmdt(const void *addr) {
    struct process *proc = current;
    uint64_t vaddr = (uint64_t)addr;
    
    if (!proc || !proc->mm) {
        return -EINVAL;
    }
    
    /* Find which segment this is */
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        struct shm_segment *seg = shm_table[i];
        if (!seg) continue;
        
        /* Check if address maps to this segment */
        uint64_t phys = mmu_resolve(proc->mm->pt, vaddr);
        if (phys == seg->pages[0]) {
            /* Unmap all pages */
            for (size_t j = 0; j < seg->num_pages; j++) {
                mmu_unmap(proc->mm->pt, vaddr + j * PAGE_SIZE);
            }
            
            seg->nattach--;
            seg->dtime = get_ticks();
            
            return 0;
        }
    }
    
    return -EINVAL;
}

int shmctl(int shmid, int cmd, void *buf) {
    struct shm_segment *seg;
    (void)buf;
    
    if (shmid < 0 || shmid >= MAX_SHM_SEGMENTS) {
        return -EINVAL;
    }
    
    seg = shm_table[shmid];
    if (!seg) {
        return -EINVAL;
    }
    
    switch (cmd) {
        case 0:  /* IPC_RMID - Remove segment */
            if (seg->nattach > 0) {
                return -EBUSY;
            }
            
            /* Free pages */
            for (size_t i = 0; i < seg->num_pages; i++) {
                pmm_free_page(seg->pages[i]);
            }
            kfree(seg->pages);
            kfree(seg);
            shm_table[shmid] = NULL;
            return 0;
            
        default:
            return -EINVAL;
    }
}