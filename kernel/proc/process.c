/*
 * Obelisk OS - Process Management
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/kmalloc.h>
#include <mm/vmm.h>
#include <fs/vfs.h>
#include <fs/file.h>

#ifndef CONFIG_PROC_TRACE
#define CONFIG_PROC_TRACE 0
#endif
#if CONFIG_PROC_TRACE
#define PROC_LOG(...) printk(KERN_INFO __VA_ARGS__)
#else
#define PROC_LOG(...) do { } while (0)
#endif

/* Process ID allocation */
pid_t next_pid = 1;

/* Process table */
struct process *pid_table[PID_MAX];

/* Global process list */
LIST_HEAD(all_processes);

/* Special processes */
struct process *init_process = NULL;
struct process *idle_process = NULL;
static char init_exec_path[128] = "/sbin/init";

/* Process cache */
static struct kmem_cache *process_cache;

/* Kernel stack size */
#define KERNEL_STACK_SIZE   (16 * 1024)

/* ==========================================================================
 * PID Management
 * ========================================================================== */

static pid_t alloc_pid(void) {
    pid_t pid;
    
    /* Simple linear search for now */
    for (pid = next_pid; pid < PID_MAX; pid++) {
        if (pid_table[pid] == NULL) {
            next_pid = pid + 1;
            return pid;
        }
    }
    
    /* Wrap around */
    for (pid = 1; pid < next_pid; pid++) {
        if (pid_table[pid] == NULL) {
            next_pid = pid + 1;
            return pid;
        }
    }
    
    return -1;  /* No PIDs available */
}

static void free_pid(pid_t pid) {
    if (pid > 0 && pid < PID_MAX) {
        pid_table[pid] = NULL;
    }
}

/* ==========================================================================
 * Credential Management
 * ========================================================================== */

struct cred *cred_create(void) {
    struct cred *cred = kzalloc(sizeof(struct cred));
    if (!cred) return NULL;
    
    cred->uid = cred->euid = cred->suid = cred->fsuid = 0;
    cred->gid = cred->egid = cred->sgid = cred->fsgid = 0;
    cred->ngroups = 0;
    cred->refcount.counter = 1;
    
    return cred;
}

struct cred *cred_clone(struct cred *old) {
    struct cred *new = kmalloc(sizeof(struct cred));
    if (!new) return NULL;
    
    memcpy(new, old, sizeof(struct cred));
    new->refcount.counter = 1;
    
    return new;
}

void cred_free(struct cred *cred) {
    if (cred && --cred->refcount.counter == 0) {
        kfree(cred);
    }
}

/* ==========================================================================
 * File Descriptor Table Management
 * ========================================================================== */

struct fd_table *fd_table_create(void) {
    struct fd_table *fdt = kzalloc(sizeof(struct fd_table));
    if (!fdt) return NULL;
    
    fdt->fds = kzalloc(sizeof(struct fd_entry) * PROC_MAX_FDS);
    if (!fdt->fds) {
        kfree(fdt);
        return NULL;
    }
    
    fdt->max_fds = PROC_MAX_FDS;
    /* Reserve 0,1,2 for stdin/stdout/stderr semantics. */
    fdt->next_fd = 3;
    fdt->lock = (spinlock_t)SPINLOCK_INIT;
    fdt->refcount.counter = 1;
    
    return fdt;
}

struct fd_table *fd_table_clone(struct fd_table *old) {
    struct fd_table *new = fd_table_create();
    if (!new) return NULL;
    
    for (size_t i = 0; i < old->max_fds; i++) {
        if (old->fds[i].file) {
            new->fds[i].file = old->fds[i].file;
            new->fds[i].flags = old->fds[i].flags;
            get_file(new->fds[i].file);
        }
    }
    
    new->next_fd = old->next_fd;
    
    return new;
}

void fd_table_free(struct fd_table *fdt) {
    if (!fdt) return;
    
    if (--fdt->refcount.counter == 0) {
        /* Close all files */
        for (size_t i = 0; i < fdt->max_fds; i++) {
            if (fdt->fds[i].file) {
                put_file(fdt->fds[i].file);
                fdt->fds[i].file = NULL;
                fdt->fds[i].flags = 0;
            }
        }
        
        kfree(fdt->fds);
        kfree(fdt);
    }
}

int fd_alloc(struct fd_table *fdt, struct file *file, int flags) {
    if (!fdt || !file) {
        return -EINVAL;
    }
    if (fdt->next_fd < 3) {
        fdt->next_fd = 3;
    }
    for (size_t i = fdt->next_fd; i < fdt->max_fds; i++) {
        if (fdt->fds[i].file == NULL) {
            fdt->fds[i].file = file;
            fdt->fds[i].flags = flags;
            fdt->next_fd = i + 1;
            return i;
        }
    }
    
    /* Search from fd 3 (stdio reserved). */
    for (size_t i = 3; i < fdt->next_fd; i++) {
        if (fdt->fds[i].file == NULL) {
            fdt->fds[i].file = file;
            fdt->fds[i].flags = flags;
            fdt->next_fd = i + 1;
            return i;
        }
    }
    
    return -EMFILE;
}

void fd_free(struct fd_table *fdt, int fd) {
    if (fd >= 3 && fd < (int)fdt->max_fds) {
        fdt->fds[fd].file = NULL;
        fdt->fds[fd].flags = 0;
        if (fd < (int)fdt->next_fd) {
            fdt->next_fd = fd;
            if (fdt->next_fd < 3) {
                fdt->next_fd = 3;
            }
        }
    }
}

struct file *fd_get(struct fd_table *fdt, int fd) {
    if (fd >= 0 && fd < (int)fdt->max_fds) {
        return fdt->fds[fd].file;
    }
    return NULL;
}

/* ==========================================================================
 * Process Creation/Destruction
 * ========================================================================== */

struct process *process_create(const char *name, uint32_t flags) {
    struct process *proc;
    
    /* Allocate process structure */
    if (process_cache) {
        proc = kmem_cache_zalloc(process_cache);
    } else {
        proc = kzalloc(sizeof(struct process));
    }
    
    if (!proc) {
        printk(KERN_ERR "Failed to allocate process structure\n");
        return NULL;
    }
    
    /* Allocate PID */
    proc->pid = alloc_pid();
    if (proc->pid < 0) {
        printk(KERN_ERR "No PIDs available\n");
        kfree(proc);
        return NULL;
    }
    
    proc->tgid = proc->pid;
    
    /* Set name */
    strncpy(proc->comm, name, sizeof(proc->comm) - 1);
    proc->comm[sizeof(proc->comm) - 1] = '\0';
    
    /* Initialize state */
    proc->state = PROC_STATE_CREATED;
    proc->flags = flags;
    proc->exit_code = 0;
    
    /* Allocate kernel stack */
    proc->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        free_pid(proc->pid);
        kfree(proc);
        return NULL;
    }
    proc->kernel_stack_size = KERNEL_STACK_SIZE;
    
    /* Set up thread info at bottom of kernel stack */
    struct thread_info *ti = (struct thread_info *)proc->kernel_stack;
    ti->process = proc;
    ti->flags = 0;
    ti->cpu = 0;
    ti->preempt_count = 0;
    ti->kernel_sp = (uint64_t)proc->kernel_stack + KERNEL_STACK_SIZE;
    
    /* Initialize context */
    memset(&proc->context, 0, sizeof(proc->context));
    proc->context.rsp = ti->kernel_sp - 8;
    
    /* Create address space for user processes */
    if (!(flags & PROC_FLAG_KERNEL)) {
        proc->mm = vmm_create_address_space();
        if (!proc->mm) {
            kfree(proc->kernel_stack);
            free_pid(proc->pid);
            kfree(proc);
            return NULL;
        }
    } else {
        proc->mm = vmm_get_kernel_address_space();
    }
    
    /* Create file descriptor table */
    proc->files = fd_table_create();
    if (!proc->files) {
        if (!(flags & PROC_FLAG_KERNEL)) {
            vmm_destroy_address_space(proc->mm);
        }
        kfree(proc->kernel_stack);
        free_pid(proc->pid);
        kfree(proc);
        return NULL;
    }
    
    /* Create credentials */
    proc->cred = cred_create();
    if (!proc->cred) {
        fd_table_free(proc->files);
        if (!(flags & PROC_FLAG_KERNEL)) {
            vmm_destroy_address_space(proc->mm);
        }
        kfree(proc->kernel_stack);
        free_pid(proc->pid);
        kfree(proc);
        return NULL;
    }
    
    /* Initialize scheduling */
    proc->priority = DEFAULT_PRIO;
    proc->nice = 0;
    proc->time_slice = DEF_TIMESLICE;
    proc->policy = SCHED_NORMAL;
    proc->runtime = 0;
    proc->start_time = get_ticks();
    
    /* Initialize signals */
    proc->pending = 0;
    proc->blocked = 0;
    for (int i = 0; i < NSIG; i++) {
        proc->sigactions[i].sa_handler = SIG_DFL;
        proc->sigactions[i].sa_mask = 0;
        proc->sigactions[i].sa_flags = 0;
    }
    
    /* Initialize relationships */
    proc->parent = current;
    INIT_LIST_HEAD(&proc->children);
    INIT_LIST_HEAD(&proc->sibling);
    INIT_LIST_HEAD(&proc->thread_group);
    INIT_LIST_HEAD(&proc->run_list);
    INIT_LIST_HEAD(&proc->sleep_list);
    proc->wakeup_tick = 0;
    
    /* Initialize wait queue */
    proc->wait_chldexit = (wait_queue_head_t)WAIT_QUEUE_HEAD_INIT(proc->wait_chldexit);
    
    /* Initialize lock */
    proc->lock = (spinlock_t)SPINLOCK_INIT;
    
    /* Add to PID table */
    pid_table[proc->pid] = proc;
    
    /* Add to global process list */
    list_add(&proc->tasks, &all_processes);
    
    /* Add to parent's children list */
    if (proc->parent) {
        list_add(&proc->sibling, &proc->parent->children);
    }
    
    PROC_LOG("proc: created '%s' (pid=%d)\n", proc->comm, proc->pid);
    
    return proc;
}

void process_destroy(struct process *proc) {
    if (!proc) return;
    
    PROC_LOG("proc: destroy '%s' (pid=%d)\n", proc->comm, proc->pid);
    
    /* Remove from all lists */
    list_del(&proc->tasks);
    list_del(&proc->sibling);
    list_del(&proc->run_list);
    list_del(&proc->sleep_list);
    
    /* Remove from PID table */
    free_pid(proc->pid);
    
    /* Free resources */
    cred_free(proc->cred);
    fd_table_free(proc->files);
    
    if (!(proc->flags & PROC_FLAG_KERNEL) && proc->mm) {
        if (--proc->mm->refcount.counter == 0) {
            vmm_destroy_address_space(proc->mm);
        }
    }
    
    kfree(proc->kernel_stack);
    
    /* Free process structure */
    if (process_cache) {
        kmem_cache_free(process_cache, proc);
    } else {
        kfree(proc);
    }
}

struct process *process_find(pid_t pid) {
    if (pid <= 0 || pid >= PID_MAX) {
        return NULL;
    }
    return pid_table[pid];
}

/* ==========================================================================
 * Idle Process
 * ========================================================================== */

static int idle_thread(void *data) {
    (void)data;
    
    while (1) {
        /* Enable interrupts and halt */
        sti();
        hlt();
    }
    
    return 0;
}

static void create_idle_process(void) {
    idle_process = process_create("idle", PROC_FLAG_KERNEL | PROC_FLAG_IDLE);
    if (!idle_process) {
        panic("Failed to create idle process");
    }
    
    idle_process->state = PROC_STATE_READY;
    {
        uint64_t top = (uint64_t)idle_process->kernel_stack + idle_process->kernel_stack_size;
        idle_process->context.rsp = top - sizeof(uint64_t);
        *(uint64_t *)idle_process->context.rsp = (uint64_t)idle_thread;
        idle_process->context.rip = (uint64_t)idle_thread;
    }
    
    printk(KERN_INFO "Created idle process (pid=%d)\n", idle_process->pid);
}

/* ==========================================================================
 * Init Process
 * ========================================================================== */

static void create_init_process(void) {
    init_process = process_create("init", PROC_FLAG_KERNEL);
    if (!init_process) {
        panic("Failed to create init process");
    }
    
    init_process->parent = NULL;
    init_process->cred->uid = init_process->cred->euid = 0;
    init_process->cred->gid = init_process->cred->egid = 0;
    
    printk(KERN_INFO "Created init process (pid=%d)\n", init_process->pid);
}

static void init_bootstrap_thread(void) {
    static const char *fallback_init_paths[] = {
        "/usr/bin/osh",
        "/bin/osh",
        "/bin/rockbox",
        "/bin/sh",
        "/sbin/installer",
        NULL
    };

    char *const argv[] = { init_exec_path, NULL };
    char *const envp[] = { NULL };
    int ret;

    printk(KERN_INFO "init: bootstrap started, attempting exec %s\n", init_exec_path);
    ret = do_execve(init_exec_path, argv, envp);
    printk(KERN_ERR "init: exec failed for %s: %d\n", init_exec_path, ret);

    if (ret == -ENOENT || ret == -ENOEXEC) {
        for (size_t i = 0; fallback_init_paths[i]; i++) {
            const char *candidate = fallback_init_paths[i];
            if (strcmp(candidate, init_exec_path) == 0) {
                continue;
            }
            char *const fargv[] = { (char *)candidate, NULL };
            printk(KERN_WARNING "init: trying fallback %s\n", candidate);
            ret = do_execve(candidate, fargv, envp);
            if (ret == 0) {
                return;
            }
            printk(KERN_ERR "init: fallback exec failed for %s: %d\n", candidate, ret);
        }
    }

    while (1) {
        scheduler_yield();
        hlt();
    }
}

void process_set_init_path(const char *path) {
    if (!path || !*path) {
        return;
    }
    strncpy(init_exec_path, path, sizeof(init_exec_path) - 1);
    init_exec_path[sizeof(init_exec_path) - 1] = '\0';
    if (init_process) {
        uint64_t top = (uint64_t)init_process->kernel_stack + init_process->kernel_stack_size;
        init_process->context.rsp = top - sizeof(uint64_t);
        *(uint64_t *)init_process->context.rsp = (uint64_t)init_bootstrap_thread;
        init_process->context.rip = (uint64_t)init_bootstrap_thread;
        init_process->state = PROC_STATE_READY;
    }
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void process_init(void) {
    printk(KERN_INFO "Initializing process management...\n");
    
    /* Clear PID table */
    memset(pid_table, 0, sizeof(pid_table));
    
    /* Create process cache */
    process_cache = kmem_cache_create("process", sizeof(struct process),
                                      sizeof(void *), 0, NULL);
    
    /* Create idle process */
    create_idle_process();
    
    /* Create init process */
    create_init_process();
    {
        uint64_t top = (uint64_t)init_process->kernel_stack + init_process->kernel_stack_size;
        init_process->context.rsp = top - sizeof(uint64_t);
        *(uint64_t *)init_process->context.rsp = (uint64_t)init_bootstrap_thread;
    }
    init_process->context.rip = (uint64_t)init_bootstrap_thread;
    init_process->state = PROC_STATE_READY;
    
    printk(KERN_INFO "Process management initialized\n");
}

void do_exit(int code) {
    struct process *p = current;
    if (p) {
        p->exit_code = code;
    }
    scheduler_exit();
    __builtin_unreachable();
}

pid_t do_wait(pid_t pid, int *status, int options) {
    struct process *parent = current;
    const int WNOHANG = 1;

    if (!parent) {
        return -ECHILD;
    }

    for (;;) {
        struct process *child = NULL;
        struct process *zombie = NULL;
        bool has_child = false;

        list_for_each_entry(child, &parent->children, sibling) {
            has_child = true;
            if (pid != -1 && child->pid != pid) {
                continue;
            }
            if (child->state == PROC_STATE_ZOMBIE) {
                zombie = child;
                break;
            }
        }

        if (zombie) {
            pid_t reaped = zombie->pid;
            int code = zombie->exit_code;
            if (status) {
                *status = code << 8;
            }
            process_destroy(zombie);
            return reaped;
        }

        if (!has_child) {
            return -ECHILD;
        }

        if (options & WNOHANG) {
            return 0;
        }

        scheduler_yield();
    }
}