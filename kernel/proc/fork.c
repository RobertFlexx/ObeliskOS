/*
 * Obelisk OS - Fork Implementation
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/vmm.h>
#include <arch/regs.h>

#ifndef CONFIG_PROC_TRACE
#define CONFIG_PROC_TRACE 0
#endif
#if CONFIG_PROC_TRACE
#define FORK_LOG(...) printk(KERN_INFO __VA_ARGS__)
#else
#define FORK_LOG(...) do { } while (0)
#endif

/* Clone flags */
#define CLONE_VM        0x00000100
#define CLONE_FS        0x00000200
#define CLONE_FILES     0x00000400
#define CLONE_SIGHAND   0x00000800
#define CLONE_PTRACE    0x00002000
#define CLONE_VFORK     0x00004000
#define CLONE_PARENT    0x00008000
#define CLONE_THREAD    0x00010000

/* Fork return point (in assembly) */
extern void fork_return(void);

/*
 * Copy process memory
 */
static int copy_mm(struct process *child, struct process *parent, uint32_t flags) {
    if (!parent || !parent->mm) {
        return -ENOMEM;
    }
    if (flags & CLONE_VM) {
        /* Share address space (for threads) */
        child->mm = parent->mm;
        child->mm->refcount.counter++;
        return 0;
    }
    
    /* Clone address space */
    child->mm = vmm_clone_address_space(parent->mm);
    if (!child->mm) {
        return -ENOMEM;
    }
    
    return 0;
}

/*
 * Copy file descriptors
 */
static int copy_files(struct process *child, struct process *parent, uint32_t flags) {
    if (!parent || !parent->files) {
        return -ENOMEM;
    }
    if (flags & CLONE_FILES) {
        /* Share file descriptor table */
        child->files = parent->files;
        child->files->refcount.counter++;
        return 0;
    }
    
    /* Clone file descriptor table */
    child->files = fd_table_clone(parent->files);
    if (!child->files) {
        return -ENOMEM;
    }
    
    return 0;
}

/*
 * Copy credentials
 */
static int copy_cred(struct process *child, struct process *parent) {
    if (!parent || !parent->cred) {
        return -ENOMEM;
    }
    child->cred = cred_clone(parent->cred);
    if (!child->cred) {
        return -ENOMEM;
    }
    return 0;
}

/*
 * Copy signal handlers
 */
static int copy_sighand(struct process *child, struct process *parent, uint32_t flags) {
    if (flags & CLONE_SIGHAND) {
        /* Share signal handlers (for threads) */
        /* TODO: Implement shared sighand structure */
    }
    
    /* Copy signal handlers */
    for (int i = 0; i < NSIG; i++) {
        child->sigactions[i] = parent->sigactions[i];
    }
    
    child->blocked = parent->blocked;
    child->pending = 0;  /* Don't inherit pending signals */
    
    return 0;
}

/*
 * Set up child's CPU context to return from fork()
 */
static void setup_child_context(struct process *child, struct process *parent,
                                uint64_t stack, struct cpu_regs *regs) {
    /* Build a synthetic context-switch return frame:
     *   [ret target=fork_return][struct cpu_regs...]
     * context_switch() returns to fork_return, which then restores cpu_regs
     * and iretq's to userspace with child fork() result in rax=0. */
    const uint64_t top = (uint64_t)child->kernel_stack + child->kernel_stack_size;
    const uint64_t frame_rsp = top - sizeof(uint64_t) - sizeof(struct cpu_regs);
    struct cpu_regs *child_regs = (struct cpu_regs *)(frame_rsp + sizeof(uint64_t));

    (void)parent;
    memset(&child->context, 0, sizeof(child->context));
    child->context.rsp = frame_rsp;
    *(uint64_t *)frame_rsp = (uint64_t)fork_return;

    memcpy(child_regs, regs, sizeof(struct cpu_regs));
    
    /* Child returns 0 */
    child_regs->rax = 0;
    
    /* Set child's user stack if specified */
    if (stack) {
        child_regs->rsp = stack;
    }
    
    /* context_switch returns using the synthetic stack frame above. */
    child->context.rip = 0;
}

/*
 * do_fork - Create a new process
 */
pid_t do_fork(uint32_t flags, uint64_t stack, struct cpu_regs *regs) {
    struct process *parent = current;
    struct process *child;
    int ret;
    
    /* Create child process */
    child = process_create(parent->comm, 0);
    if (!child) {
        return -ENOMEM;
    }
    
    /* Free the default allocations, we'll copy/share from parent. */
    if (child->mm && !(child->flags & PROC_FLAG_KERNEL)) {
        vmm_destroy_address_space(child->mm);
        child->mm = NULL;
    }
    if (child->files) {
        fd_table_free(child->files);
        child->files = NULL;
    }
    if (child->cred) {
        cred_free(child->cred);
        child->cred = NULL;
    }
    
    /* Copy memory */
    ret = copy_mm(child, parent, flags);
    if (ret < 0) {
        goto fail;
    }
    
    /* Copy files */
    ret = copy_files(child, parent, flags);
    if (ret < 0) {
        goto fail;
    }
    
    /* Copy credentials */
    ret = copy_cred(child, parent);
    if (ret < 0) {
        goto fail;
    }
    
    /* Copy signals */
    ret = copy_sighand(child, parent, flags);
    if (ret < 0) {
        goto fail;
    }
    
    /* Copy filesystem info */
    child->cwd = parent->cwd;
    child->root = parent->root;
    child->umask = parent->umask;
    
    /* Set up child context */
    setup_child_context(child, parent, stack, regs);
    
    /* process_create() already linked child under current parent.
     * Re-parent only when explicitly requested. */
    if (flags & CLONE_PARENT) {
        if (child->parent) {
            list_del(&child->sibling);
        }
        child->parent = parent->parent;
        if (child->parent) {
            list_add(&child->sibling, &child->parent->children);
        }
    }
    
    /* Handle CLONE_THREAD */
    if (flags & CLONE_THREAD) {
        child->tgid = parent->tgid;
        list_add(&child->thread_group, &parent->thread_group);
    }
    
    /* Make child runnable */
    scheduler_enqueue(child);
    
    /* Handle CLONE_VFORK */
    if (flags & CLONE_VFORK) {
        parent->flags |= PROC_FLAG_VFORKED;
        /* TODO: Wait for child to exec or exit */
    }
    
    FORK_LOG("fork: parent=%d, child=%d\n", parent->pid, child->pid);
    
    return child->pid;
    
fail:
    process_destroy(child);
    return ret;
}

/*
 * sys_fork - Fork system call
 */
pid_t sys_fork(struct cpu_regs *regs) {
    return do_fork(0, 0, regs);
}

/*
 * sys_vfork - Vfork system call
 */
pid_t sys_vfork(struct cpu_regs *regs) {
    return do_fork(CLONE_VFORK | CLONE_VM, 0, regs);
}

/*
 * sys_clone - Clone system call
 */
pid_t sys_clone(uint32_t flags, uint64_t stack, struct cpu_regs *regs) {
    return do_fork(flags, stack, regs);
}