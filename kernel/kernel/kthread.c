/*
 * Obelisk OS - Kernel Threads
 * From Axioms, Order.
 *
 * Provides kernel-mode threading support.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/kmalloc.h>

/* Kernel thread stack size */
#define KTHREAD_STACK_SIZE  (16 * 1024)

/* Kernel thread states */
enum kthread_state {
    KTHREAD_CREATED,
    KTHREAD_RUNNING,
    KTHREAD_STOPPED,
    KTHREAD_ZOMBIE
};

/* Kernel thread structure */
struct kthread {
    pid_t tid;                      /* Thread ID */
    char name[32];                  /* Thread name */
    enum kthread_state state;       /* Thread state */
    
    int (*func)(void *);            /* Thread function */
    void *data;                     /* Thread data */
    int result;                     /* Return value */
    
    void *stack;                    /* Kernel stack */
    struct cpu_context context;     /* CPU context */
    
    bool should_stop;               /* Stop flag */
    bool parked;                    /* Parked flag */
    
    wait_queue_head_t wait;         /* Wait queue for joining */
    
    struct list_head list;          /* Global kthread list */
    spinlock_t lock;
};

/* Global kthread list */
static LIST_HEAD(kthread_list);
static pid_t next_tid = 1;

/* Kthread cache */
static struct kmem_cache *kthread_cache;
static void wake_up_process(struct kthread *kt);

/* Kthread entry point */
static void kthread_entry(void) {
    struct kthread *kt = NULL;  /* TODO: Get from TLS */
    
    if (kt && kt->func) {
        kt->state = KTHREAD_RUNNING;
        kt->result = kt->func(kt->data);
        kt->state = KTHREAD_ZOMBIE;
    }
    
    /* Wake up any threads waiting for us */
    /* TODO: Implement wait queue wakeup */
    
    /* Exit */
    scheduler_exit();
}

/*
 * kthread_create - Create a new kernel thread
 * @func: Thread function
 * @data: Data to pass to thread function
 * @name: Thread name
 *
 * Returns: kthread structure or NULL on failure
 */
struct kthread *kthread_create(int (*func)(void *), void *data, 
                               const char *name, ...) {
    struct kthread *kt;
    va_list args;
    
    /* Allocate kthread structure */
    if (kthread_cache) {
        kt = kmem_cache_zalloc(kthread_cache);
    } else {
        kt = kzalloc(sizeof(struct kthread));
    }
    
    if (!kt) {
        return NULL;
    }
    
    /* Allocate stack */
    kt->stack = kmalloc(KTHREAD_STACK_SIZE);
    if (!kt->stack) {
        kfree(kt);
        return NULL;
    }
    
    /* Initialize thread */
    kt->tid = next_tid++;
    kt->state = KTHREAD_CREATED;
    kt->func = func;
    kt->data = data;
    kt->result = 0;
    kt->should_stop = false;
    kt->parked = false;
    kt->lock = (spinlock_t)SPINLOCK_INIT;
    
    /* Set name */
    va_start(args, name);
    vsnprintf(kt->name, sizeof(kt->name), name, args);
    va_end(args);
    
    /* Initialize context */
    memset(&kt->context, 0, sizeof(kt->context));
    kt->context.rsp = (uint64_t)kt->stack + KTHREAD_STACK_SIZE - sizeof(uint64_t);
    *(uint64_t *)kt->context.rsp = (uint64_t)kthread_entry;
    kt->context.rip = (uint64_t)kthread_entry;
    
    /* Add to global list */
    list_add(&kt->list, &kthread_list);
    
    printk(KERN_DEBUG "Created kthread '%s' (tid=%d)\n", kt->name, kt->tid);
    
    return kt;
}

/*
 * kthread_run - Create and start a kernel thread
 */
struct kthread *kthread_run(int (*func)(void *), void *data,
                            const char *name, ...) {
    struct kthread *kt;
    va_list args;
    char buf[32];
    
    va_start(args, name);
    vsnprintf(buf, sizeof(buf), name, args);
    va_end(args);
    
    kt = kthread_create(func, data, "%s", buf);
    if (kt) {
        wake_up_process(kt);
    }
    
    return kt;
}

/*
 * kthread_stop - Stop a kernel thread
 */
int kthread_stop(struct kthread *kt) {
    if (!kt) return -EINVAL;
    
    kt->should_stop = true;
    
    /* Wake up the thread if it's sleeping */
    wake_up_process(kt);
    
    /* Wait for thread to exit */
    while (kt->state != KTHREAD_ZOMBIE) {
        scheduler_yield();
    }
    
    return kt->result;
}

/*
 * kthread_should_stop - Check if thread should stop
 */
bool kthread_should_stop(void) {
    struct kthread *kt = NULL;  /* TODO: Get current kthread */
    return kt ? kt->should_stop : false;
}

/*
 * kthread_park - Park a kernel thread
 */
void kthread_park(struct kthread *kt) {
    if (!kt) return;
    
    kt->parked = true;
    
    while (kt->parked && !kt->should_stop) {
        scheduler_yield();
    }
}

/*
 * kthread_unpark - Unpark a kernel thread
 */
void kthread_unpark(struct kthread *kt) {
    if (!kt) return;
    
    kt->parked = false;
    wake_up_process(kt);
}

/*
 * wake_up_process - Wake up a process/thread
 */
static void wake_up_process(struct kthread *kt) {
    if (!kt) return;
    
    /* TODO: Add to scheduler run queue */
}

/*
 * kthread_init - Initialize kthread subsystem
 */
void kthread_init(void) {
    /* Create kthread cache */
    kthread_cache = kmem_cache_create("kthread", sizeof(struct kthread),
                                      sizeof(void *), 0, NULL);
    
    printk(KERN_INFO "Kernel thread subsystem initialized\n");
}

/*
 * kthread_destroy - Destroy a kthread structure
 */
void kthread_destroy(struct kthread *kt) {
    if (!kt) return;
    
    /* Remove from global list */
    list_del(&kt->list);
    
    /* Free stack */
    kfree(kt->stack);
    
    /* Free structure */
    if (kthread_cache) {
        kmem_cache_free(kthread_cache, kt);
    } else {
        kfree(kt);
    }
}