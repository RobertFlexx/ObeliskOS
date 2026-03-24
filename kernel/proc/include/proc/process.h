/*
 * Obelisk OS - Process Management Header
 * From Axioms, Order.
 */

#ifndef _PROC_PROCESS_H
#define _PROC_PROCESS_H

#include <obelisk/types.h>
#include <obelisk/limits.h>
#include <arch/regs.h>
#include <mm/vmm.h>

/* Process states */
typedef enum {
    PROC_STATE_CREATED,     /* Just created, not yet runnable */
    PROC_STATE_READY,       /* Ready to run */
    PROC_STATE_RUNNING,     /* Currently running */
    PROC_STATE_BLOCKED,     /* Waiting for event */
    PROC_STATE_SLEEPING,    /* Sleeping */
    PROC_STATE_ZOMBIE,      /* Terminated, waiting for parent */
    PROC_STATE_DEAD         /* Can be freed */
} process_state_t;

/* Process flags */
#define PROC_FLAG_KERNEL        BIT(0)  /* Kernel process */
#define PROC_FLAG_IDLE          BIT(1)  /* Idle process */
#define PROC_FLAG_KTHREAD       BIT(2)  /* Kernel thread */
#define PROC_FLAG_EXITING       BIT(3)  /* Process is exiting */
#define PROC_FLAG_VFORKED       BIT(4)  /* vfork in progress */
#define PROC_FLAG_SIGNALED      BIT(5)  /* Has pending signals */
#define PROC_FLAG_TRACED        BIT(6)  /* Being traced */

/* Maximum open files per process */
#define PROC_MAX_FDS            1024

/* Signal definitions */
#define SIGHUP      1
#define SIGINT      2
#define SIGQUIT     3
#define SIGILL      4
#define SIGTRAP     5
#define SIGABRT     6
#define SIGBUS      7
#define SIGFPE      8
#define SIGKILL     9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGSTKFLT   16
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define SIGTTIN     21
#define SIGTTOU     22
#define SIGURG      23
#define SIGXCPU     24
#define SIGXFSZ     25
#define SIGVTALRM   26
#define SIGPROF     27
#define SIGWINCH    28
#define SIGIO       29
#define SIGPWR      30
#define SIGSYS      31
#define NSIG        32

/* Signal set */
typedef uint64_t sigset_t;

/* Signal handler */
typedef void (*sighandler_t)(int);

#define SIG_DFL     ((sighandler_t)0)
#define SIG_IGN     ((sighandler_t)1)
#define SIG_ERR     ((sighandler_t)-1)

/* Signal action */
struct sigaction {
    sighandler_t sa_handler;
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

/* File descriptor entry */
struct fd_entry {
    struct file *file;      /* File pointer */
    uint32_t flags;         /* File descriptor flags */
};

/* File descriptor table */
struct fd_table {
    struct fd_entry *fds;   /* Array of file descriptors */
    size_t max_fds;         /* Maximum number of FDs */
    size_t next_fd;         /* Next available FD */
    spinlock_t lock;        /* Table lock */
    atomic_t refcount;      /* Reference count (for sharing) */
};

/* Process credentials */
struct cred {
    uid_t uid;              /* Real user ID */
    uid_t euid;             /* Effective user ID */
    uid_t suid;             /* Saved user ID */
    uid_t fsuid;            /* Filesystem user ID */
    gid_t gid;              /* Real group ID */
    gid_t egid;             /* Effective group ID */
    gid_t sgid;             /* Saved group ID */
    gid_t fsgid;            /* Filesystem group ID */
    uint32_t ngroups;       /* Number of supplementary groups */
    gid_t groups[NGROUPS_MAX]; /* Supplementary groups */
    atomic_t refcount;
};

/* Process structure */
struct process {
    /* Identity */
    pid_t pid;                      /* Process ID */
    pid_t tgid;                     /* Thread group ID */
    char comm[16];                  /* Process name */
    char exec_path[PATH_MAX];       /* Best-effort executable path */
    
    /* State */
    process_state_t state;          /* Current state */
    uint32_t flags;                 /* Process flags */
    int exit_code;                  /* Exit code */
    
    /* CPU context */
    struct cpu_context context;     /* Saved CPU context */
    void *kernel_stack;             /* Kernel stack */
    size_t kernel_stack_size;       /* Kernel stack size */
    
    /* Memory management */
    struct address_space *mm;       /* Address space */
    
    /* Files */
    struct fd_table *files;         /* File descriptor table */
    struct dentry *cwd;             /* Current working directory */
    struct dentry *root;            /* Root directory */
    mode_t umask;                   /* File creation mask */
    
    /* Credentials */
    struct cred *cred;              /* Process credentials */
    
    /* Scheduling */
    int priority;                   /* Static priority */
    int nice;                       /* Nice value (-20 to 19) */
    uint64_t runtime;               /* Total runtime (ns) */
    uint64_t start_time;            /* Start time (ns since boot) */
    uint64_t last_run;              /* Last run time */
    int time_slice;                 /* Remaining time slice */
    int policy;                     /* Scheduling policy */
    
    /* Signals */
    sigset_t pending;               /* Pending signals */
    sigset_t blocked;               /* Blocked signals */
    struct sigaction sigactions[NSIG]; /* Signal handlers */
    
    /* Process relationships */
    struct process *parent;         /* Parent process */
    struct list_head children;      /* Child processes */
    struct list_head sibling;       /* Sibling list link */
    
    /* Thread group */
    struct list_head thread_group;  /* Thread group members */
    
    /* Wait queue for wait() */
    wait_queue_head_t wait_chldexit; /* Wait for child exit */
    
    /* Statistics */
    uint64_t utime;                 /* User time (ticks) */
    uint64_t stime;                 /* System time (ticks) */
    uint64_t cutime;                /* Children user time */
    uint64_t cstime;                /* Children system time */
    uint64_t min_flt;               /* Minor page faults */
    uint64_t maj_flt;               /* Major page faults */
    
    /* Scheduler list entry */
    struct list_head run_list;      /* Run queue link */
    
    /* Global process list */
    struct list_head tasks;         /* All processes link */
    
    /* Lock */
    spinlock_t lock;
};

/* Kernel thread info (at bottom of kernel stack) */
struct thread_info {
    struct process *process;        /* Current process */
    uint32_t flags;                 /* Thread flags */
    uint32_t cpu;                   /* Current CPU */
    int preempt_count;              /* Preemption disable count */
    uint64_t kernel_sp;             /* Kernel stack pointer */
};

/* Thread info flags */
#define TIF_NEED_RESCHED    BIT(0)  /* Reschedule needed */
#define TIF_SIGPENDING      BIT(1)  /* Signal pending */
#define TIF_SYSCALL_TRACE   BIT(2)  /* Syscall tracing */
#define TIF_NOTIFY_RESUME   BIT(3)  /* Notify on resume */

/* Global variables */
extern struct process *init_process;
extern struct process *idle_process;
extern pid_t next_pid;

/* Process table */
#define PID_MAX             32768
extern struct process *pid_table[PID_MAX];

/* Current process/thread */
struct thread_info *current_thread_info(void);
struct process *current_process(void);

#define current     current_process()

/* Process management functions */
void process_init(void);
void process_set_init_path(const char *path);
struct process *process_create(const char *name, uint32_t flags);
void process_destroy(struct process *proc);
struct process *process_find(pid_t pid);

/* Process lifecycle */
pid_t do_fork(uint32_t flags, uint64_t stack, struct cpu_regs *regs);
int do_execve(const char *filename, char *const argv[], char *const envp[]);
void do_exit(int code);
pid_t do_wait(pid_t pid, int *status, int options);

/* Credential management */
struct cred *cred_create(void);
struct cred *cred_clone(struct cred *cred);
void cred_free(struct cred *cred);

/* File descriptor management */
struct fd_table *fd_table_create(void);
struct fd_table *fd_table_clone(struct fd_table *fdt);
void fd_table_free(struct fd_table *fdt);
int fd_alloc(struct fd_table *fdt, struct file *file, int flags);
void fd_free(struct fd_table *fdt, int fd);
struct file *fd_get(struct fd_table *fdt, int fd);

/* Signal management */
int do_kill(pid_t pid, int sig);
int do_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
void do_signal_deliver(struct process *proc);

#endif /* _PROC_PROCESS_H */