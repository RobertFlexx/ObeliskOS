/*
 * Obelisk OS - Process Scheduler
 * From Axioms, Order.
 *
 * Implements a table-driven, policy-configurable scheduler.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <arch/cpu.h>

/* Global run queue */
struct run_queue runqueue;

/* Current scheduling policy */
struct sched_policy_ops *current_policy = NULL;

/* Registered policies */
#define MAX_SCHED_POLICIES  8
static struct sched_policy_ops *policies[MAX_SCHED_POLICIES];
static int num_policies = 0;

/* Timer tick rate (Hz) */
#define HZ  100
#define TICK_MS (1000 / HZ)

/* Scheduler statistics */
static uint64_t total_switches = 0;
static struct thread_info scheduler_ti;

struct thread_info *current_thread_info(void) {
    return &scheduler_ti;
}

struct process *current_process(void) {
    return runqueue.curr;
}

/* ==========================================================================
 * Default Round-Robin Policy
 * ========================================================================== */

static void rr_init(void) {
    /* Nothing to initialize */
}

static void rr_enqueue(struct run_queue *rq, struct process *p) {
    int prio = p->priority;
    
    if (prio < 0) prio = 0;
    if (prio >= MAX_PRIO) prio = MAX_PRIO - 1;
    
    list_add_tail(&p->run_list, &rq->queues[prio]);
    rq->bitmap[prio / 64] |= (1UL << (prio % 64));
    rq->nr_running++;
}

static void rr_dequeue(struct run_queue *rq, struct process *p) {
    int prio = p->priority;
    
    list_del(&p->run_list);
    INIT_LIST_HEAD(&p->run_list);
    
    if (list_empty(&rq->queues[prio])) {
        rq->bitmap[prio / 64] &= ~(1UL << (prio % 64));
    }
    
    if (rq->nr_running > 0) {
        rq->nr_running--;
    }
}

static struct process *rr_pick_next(struct run_queue *rq) {
    /* Find highest priority non-empty queue */
    for (int i = 0; i < MAX_PRIO; i++) {
        if (!list_empty(&rq->queues[i])) {
            return list_first_entry(&rq->queues[i], struct process, run_list);
        }
    }
    
    return rq->idle;
}

static void rr_yield(struct run_queue *rq, struct process *p) {
    if (p && p != rq->idle) {
        /* Move to end of queue */
        rr_dequeue(rq, p);
        p->time_slice = DEF_TIMESLICE;
        rr_enqueue(rq, p);
    }
}

static void rr_tick(struct run_queue *rq, struct process *p) {
    if (p && p != rq->idle) {
        p->time_slice--;
        
        if (p->time_slice <= 0) {
            /* Time slice expired, need to reschedule */
            struct thread_info *ti = current_thread_info();
            ti->flags |= TIF_NEED_RESCHED;
        }
    }
}

static int rr_time_slice(struct process *p) {
    /* Calculate time slice based on nice value */
    int slice = DEF_TIMESLICE;
    
    if (p->nice < 0) {
        slice = DEF_TIMESLICE + (-p->nice * 2);
    } else if (p->nice > 0) {
        slice = DEF_TIMESLICE - (p->nice / 2);
    }
    
    if (slice < MIN_TIMESLICE) slice = MIN_TIMESLICE;
    if (slice > MAX_TIMESLICE) slice = MAX_TIMESLICE;
    
    return slice;
}

static void rr_set_priority(struct process *p, int prio) {
    if (prio < 0) prio = 0;
    if (prio >= MAX_PRIO) prio = MAX_PRIO - 1;
    
    if (p->state == PROC_STATE_READY || p->state == PROC_STATE_RUNNING) {
        rr_dequeue(&runqueue, p);
        p->priority = prio;
        rr_enqueue(&runqueue, p);
    } else {
        p->priority = prio;
    }
}

static struct sched_policy_ops rr_policy = {
    .name = "round-robin",
    .init = rr_init,
    .enqueue = rr_enqueue,
    .dequeue = rr_dequeue,
    .pick_next = rr_pick_next,
    .yield = rr_yield,
    .tick = rr_tick,
    .time_slice = rr_time_slice,
    .set_priority = rr_set_priority,
};

/* ==========================================================================
 * Core Scheduler Functions
 * ========================================================================== */

void scheduler_enqueue(struct process *p) {
    if (!p || p == runqueue.idle) return;
    
    p->state = PROC_STATE_READY;
    p->time_slice = current_policy->time_slice(p);
    current_policy->enqueue(&runqueue, p);
}

void scheduler_dequeue(struct process *p) {
    if (!p || p == runqueue.idle) return;
    
    current_policy->dequeue(&runqueue, p);
}

/* Main scheduling function */
void schedule(void) {
    struct process *prev, *next;
    struct run_queue *rq = &runqueue;
    
    cli();  /* Disable interrupts */
    
    prev = rq->curr;
    
    /* Handle previous process */
    if (prev) {
        if (prev->state == PROC_STATE_RUNNING) {
            prev->state = PROC_STATE_READY;
            current_policy->enqueue(rq, prev);
        }
    }
    
    /* Pick next process */
    next = current_policy->pick_next(rq);
    
    if (next != prev) {
        rq->nr_switches++;
        total_switches++;
        
        if (next) {
            /* Dequeue if not idle */
            if (next != rq->idle && next->state == PROC_STATE_READY) {
                current_policy->dequeue(rq, next);
            }
            
            next->state = PROC_STATE_RUNNING;
            next->last_run = get_ticks();
            rq->curr = next;
            scheduler_ti.process = next;
            
            /* Switch address space if needed */
            if (prev && prev->mm != next->mm && next->mm) {
                mmu_switch(next->mm->pt);
            }
            
            /* Update TSS RSP0 for user->kernel transitions */
            extern void tss_set_rsp0(uint64_t);
            tss_set_rsp0((uint64_t)next->kernel_stack + next->kernel_stack_size);
            
            /* Context switch */
            if (prev) {
                context_switch(&prev->context, &next->context);
            } else {
                context_switch_initial(&next->context);
            }
            /* context_switch restores task register state and does not obey
             * normal C callee-saved assumptions from the compiler's view. */
            __asm__ __volatile__("" ::: "rbx", "r12", "r13", "r14", "r15", "memory");
        }
    }
    
    /* Clear reschedule flag */
    current_thread_info()->flags &= ~TIF_NEED_RESCHED;
    
    sti();  /* Re-enable interrupts */
}

void scheduler_yield(void) {
    /* Do not pre-move the current task in run queues here.
     * schedule() already handles RUNNING->READY enqueue, and doing both
     * causes double-enqueue list corruption under wait/yield loops. */
    schedule();
}

void scheduler_tick(void) {
    struct process *p = runqueue.curr;
    
    if (p) {
        p->runtime += TICK_MS;
        
        if (p->flags & PROC_FLAG_KERNEL) {
            p->stime++;
        } else {
            p->utime++;
        }
        
        current_policy->tick(&runqueue, p);
    }
    
    runqueue.clock += TICK_MS;
}

void scheduler_exit(void) {
    struct process *p = current;
    
    if (p) {
        p->state = PROC_STATE_ZOMBIE;
        
        /* Wake up parent */
        if (p->parent) {
            /* TODO: Wake parent from wait() */
        }
        
        schedule();
    }
    
    /* Should never reach here */
    panic("scheduler_exit returned");
}

/* ==========================================================================
 * Sleep/Wake
 * ========================================================================== */

void scheduler_sleep(struct process *p, wait_queue_head_t *wq) {
    if (!p) return;
    
    cli();
    
    p->state = PROC_STATE_BLOCKED;
    scheduler_dequeue(p);
    
    /* TODO: Add to wait queue */
    (void)wq;
    
    sti();
    
    schedule();
}

void scheduler_wake(struct process *p) {
    if (!p) return;
    
    if (p->state == PROC_STATE_BLOCKED || p->state == PROC_STATE_SLEEPING) {
        scheduler_enqueue(p);
    }
}

void scheduler_sleep_timeout(struct process *p, uint64_t timeout_ms) {
    if (!p) return;
    
    p->state = PROC_STATE_SLEEPING;
    scheduler_dequeue(p);
    
    /* TODO: Set up timer to wake process */
    (void)timeout_ms;
    
    schedule();
}

/* ==========================================================================
 * Priority Management
 * ========================================================================== */

void scheduler_set_priority(struct process *p, int prio) {
    if (current_policy->set_priority) {
        current_policy->set_priority(p, prio);
    }
}

void scheduler_set_nice(struct process *p, int nice) {
    if (nice < MIN_NICE) nice = MIN_NICE;
    if (nice > MAX_NICE) nice = MAX_NICE;
    
    p->nice = nice;
    
    /* Recalculate priority */
    int prio = DEFAULT_PRIO + nice;
    scheduler_set_priority(p, prio);
}

int scheduler_get_priority(struct process *p) {
    return p ? p->priority : 0;
}

/* ==========================================================================
 * Policy Management
 * ========================================================================== */

int scheduler_set_policy(struct process *p, int policy) {
    if (policy < 0 || policy >= num_policies) {
        return -EINVAL;
    }
    
    p->policy = policy;
    return 0;
}

int scheduler_get_policy(struct process *p) {
    return p ? p->policy : SCHED_NORMAL;
}

int scheduler_register_policy(struct sched_policy_ops *ops) {
    if (num_policies >= MAX_SCHED_POLICIES) {
        return -ENOMEM;
    }
    
    policies[num_policies++] = ops;
    
    if (ops->init) {
        ops->init();
    }
    
    printk(KERN_INFO "Registered scheduling policy: %s\n", ops->name);
    
    return 0;
}

/* ==========================================================================
 * Statistics
 * ========================================================================== */

uint64_t scheduler_get_switches(void) {
    return total_switches;
}

uint64_t scheduler_get_runtime(struct process *p) {
    return p ? p->runtime : 0;
}

/* ==========================================================================
 * Timer
 * ========================================================================== */

static void __unused timer_handler(void) {
    scheduler_tick();
    
    /* Check if reschedule needed */
    if (current_thread_info()->flags & TIF_NEED_RESCHED) {
        schedule();
    }
}

void timer_init(void) {
    printk(KERN_INFO "Initializing timer (HZ=%d)...\n", HZ);
    
    /* Program PIT for HZ ticks per second */
    uint16_t divisor = 1193182 / HZ;
    
    outb(0x43, 0x36);               /* Channel 0, rate generator */
    outb(0x40, divisor & 0xFF);     /* Low byte */
    outb(0x40, (divisor >> 8));     /* High byte */
    
    /* Enable timer IRQ */
    extern void irq_enable(uint8_t);
    irq_enable(0);
    
    printk(KERN_INFO "Timer initialized\n");
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void scheduler_init(void) {
    printk(KERN_INFO "Initializing scheduler...\n");
    
    /* Initialize run queue */
    memset(&runqueue, 0, sizeof(runqueue));
    memset(&scheduler_ti, 0, sizeof(scheduler_ti));
    runqueue.lock = (spinlock_t)SPINLOCK_INIT;
    
    for (int i = 0; i < MAX_PRIO; i++) {
        INIT_LIST_HEAD(&runqueue.queues[i]);
    }
    
    /* Register default policy */
    scheduler_register_policy(&rr_policy);
    current_policy = &rr_policy;
    
    /* Set idle process */
    runqueue.idle = idle_process;
    runqueue.curr = NULL;
    scheduler_ti.process = NULL;
    
    printk(KERN_INFO "Scheduler initialized with policy: %s\n",
           current_policy->name);
}

void scheduler_start(void) {
    printk(KERN_INFO "Starting scheduler...\n");
    
    /* Enqueue init process */
    if (init_process) {
        scheduler_enqueue(init_process);
    }
    
    /* Start scheduling */
    schedule();
}