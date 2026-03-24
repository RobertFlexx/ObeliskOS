/*
 * Obelisk OS - Scheduler Header
 * From Axioms, Order.
 */

#ifndef _PROC_SCHEDULER_H
#define _PROC_SCHEDULER_H

#include <obelisk/types.h>
#include <proc/process.h>

/* Scheduling policies */
#define SCHED_NORMAL    0       /* Standard round-robin */
#define SCHED_FIFO      1       /* First-in, first-out real-time */
#define SCHED_RR        2       /* Round-robin real-time */
#define SCHED_BATCH     3       /* Batch processing */
#define SCHED_IDLE      5       /* Idle priority */

/* Priority levels */
#define MAX_NICE        19
#define MIN_NICE        -20
#define NICE_WIDTH      40
#define MAX_RT_PRIO     100
#define MAX_PRIO        (MAX_RT_PRIO + NICE_WIDTH)
#define DEFAULT_PRIO    (MAX_RT_PRIO + NICE_WIDTH / 2)

/* Time slice constants (in ms) */
#define MIN_TIMESLICE   1
#define DEF_TIMESLICE   10
#define MAX_TIMESLICE   100

/* Scheduler run queue */
struct run_queue {
    spinlock_t lock;                /* Queue lock */
    uint64_t nr_running;            /* Number of runnable processes */
    uint64_t nr_switches;           /* Context switch count */
    
    /* Priority arrays */
    struct list_head queues[MAX_PRIO];  /* Per-priority queues */
    uint64_t bitmap[MAX_PRIO / 64 + 1]; /* Priority bitmap */
    
    /* Current and idle processes */
    struct process *curr;           /* Currently running */
    struct process *idle;           /* Idle process */
    
    /* Statistics */
    uint64_t total_runtime;         /* Total runtime */
    uint64_t clock;                 /* Scheduler clock */
};

/* Scheduler policy operations */
struct sched_policy_ops {
    const char *name;
    
    /* Initialize policy */
    void (*init)(void);
    
    /* Enqueue a process */
    void (*enqueue)(struct run_queue *rq, struct process *p);
    
    /* Dequeue a process */
    void (*dequeue)(struct run_queue *rq, struct process *p);
    
    /* Pick next process to run */
    struct process *(*pick_next)(struct run_queue *rq);
    
    /* Called when process is preempted */
    void (*yield)(struct run_queue *rq, struct process *p);
    
    /* Timer tick */
    void (*tick)(struct run_queue *rq, struct process *p);
    
    /* Calculate time slice */
    int (*time_slice)(struct process *p);
    
    /* Set priority */
    void (*set_priority)(struct process *p, int prio);
};

/* Global scheduler state */
extern struct run_queue runqueue;
extern struct sched_policy_ops *current_policy;

/* Scheduler initialization */
void scheduler_init(void);
void scheduler_start(void);

/* Process scheduling */
void scheduler_enqueue(struct process *p);
void scheduler_dequeue(struct process *p);
void schedule(void);
void scheduler_yield(void);
void scheduler_tick(void);
void scheduler_exit(void);

/* Sleep/wake */
void scheduler_sleep(struct process *p, wait_queue_head_t *wq);
void scheduler_wake(struct process *p);
void scheduler_sleep_timeout(struct process *p, uint64_t timeout_ms);

/* Priority management */
void scheduler_set_priority(struct process *p, int prio);
void scheduler_set_nice(struct process *p, int nice);
int scheduler_get_priority(struct process *p);

/* Policy management */
int scheduler_set_policy(struct process *p, int policy);
int scheduler_get_policy(struct process *p);
int scheduler_register_policy(struct sched_policy_ops *ops);

/* CPU management */
void scheduler_cpu_online(int cpu);
void scheduler_cpu_offline(int cpu);

/* Load balancing (future SMP) */
void scheduler_balance(void);

/* Timer initialization */
void timer_init(void);

/* Statistics */
uint64_t scheduler_get_switches(void);
uint64_t scheduler_get_runtime(struct process *p);

/* Idle loop */
void cpu_idle(void);

#endif /* _PROC_SCHEDULER_H */