/*
 * Obelisk OS - Core Type Definitions
 * From Axioms, Order.
 */

#ifndef _OBELISK_TYPES_H
#define _OBELISK_TYPES_H

/* Standard integer types */
typedef signed char         int8_t;
typedef unsigned char       uint8_t;
typedef signed short        int16_t;
typedef unsigned short      uint16_t;
typedef signed int          int32_t;
typedef unsigned int        uint32_t;
typedef signed long long    int64_t;
typedef unsigned long long  uint64_t;

/* Size types */
typedef uint64_t            size_t;
typedef int64_t             ssize_t;
typedef int64_t             ptrdiff_t;
typedef uint64_t            uintptr_t;
typedef int64_t             intptr_t;

/* Process and user types */
typedef int32_t             pid_t;
typedef uint32_t            uid_t;
typedef uint32_t            gid_t;
typedef uint32_t            mode_t;
typedef int64_t             off_t;
typedef int64_t             loff_t;
typedef uint64_t            ino_t;
typedef uint64_t            dev_t;
typedef uint32_t            nlink_t;
typedef int64_t             blkcnt_t;
typedef int64_t             blksize_t;
typedef int64_t             time_t;

/* Boolean type */
typedef _Bool               bool;
#define true                1
#define false               0

/* NULL pointer */
#ifndef NULL
#define NULL                ((void *)0)
#endif

/* Limits */
#define INT8_MIN            (-128)
#define INT8_MAX            127
#define UINT8_MAX           255
#define INT16_MIN           (-32768)
#define INT16_MAX           32767
#define UINT16_MAX          65535
#define INT32_MIN           (-2147483647 - 1)
#define INT32_MAX           2147483647
#define UINT32_MAX          4294967295U
#define INT64_MIN           (-9223372036854775807LL - 1)
#define INT64_MAX           9223372036854775807LL
#define UINT64_MAX          18446744073709551615ULL

#define SIZE_MAX            UINT64_MAX
#define SSIZE_MAX           INT64_MAX

/* Alignment and packing */
#define __packed            __attribute__((packed))
#define __aligned(x)        __attribute__((aligned(x)))
#define __section(x)        __attribute__((section(x)))
#define __used              __attribute__((used))
#define __unused            __attribute__((unused))
#define __weak              __attribute__((weak))
#define __noreturn          __attribute__((noreturn))
#define __always_inline     __attribute__((always_inline)) inline
#define __noinline          __attribute__((noinline))

/* Likely/unlikely for branch prediction */
#define likely(x)           __builtin_expect(!!(x), 1)
#define unlikely(x)         __builtin_expect(!!(x), 0)

/* Container of macro */
#define container_of(ptr, type, member) ({                      \
const typeof(((type *)0)->member) *__mptr = (ptr);          \
(type *)((char *)__mptr - offsetof(type, member)); })

/* Offset of macro */
#define offsetof(type, member)  __builtin_offsetof(type, member)

/* Array size */
#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))

/* Min/max macros */
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))

/* Bit manipulation */
#define BIT(n)              (1ULL << (n))
#define BITS_PER_BYTE       8
#define BITS_PER_LONG       64
#define BITS_TO_LONGS(nr)   (((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)

/* Alignment helpers */
#define ALIGN_UP(x, align)      (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align)    ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align)    (((x) & ((align) - 1)) == 0)

/* Page size */
#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGE_MASK           (~(PAGE_SIZE - 1))

/* Memory barriers */
#define barrier()           __asm__ volatile("" ::: "memory")
#define mb()                __asm__ volatile("mfence" ::: "memory")
#define rmb()               __asm__ volatile("lfence" ::: "memory")
#define wmb()               __asm__ volatile("sfence" ::: "memory")

/* Atomic operations forward declaration */
typedef struct {
    volatile int64_t counter;
} atomic64_t;

typedef struct {
    volatile int32_t counter;
} atomic_t;

/* Spinlock type */
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

/* Device number helpers */
#define MKDEV(major, minor) ((((dev_t)(major)) << 20) | ((dev_t)(minor)))
#define MAJOR(dev)          ((unsigned int)((dev) >> 20))
#define MINOR(dev)          ((unsigned int)((dev) & ((1U << 20) - 1)))

#define SPINLOCK_INIT       { .lock = 0 }

/* List head structure */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name)      struct list_head name = LIST_HEAD_INIT(name)

static __always_inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static __always_inline void __list_add(struct list_head *new_node,
                                       struct list_head *prev,
                                       struct list_head *next) {
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

static __always_inline void list_add(struct list_head *new_node, struct list_head *head) {
    __list_add(new_node, head, head->next);
}

static __always_inline void list_add_tail(struct list_head *new_node, struct list_head *head) {
    __list_add(new_node, head->prev, head);
}

static __always_inline void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    prev->next = next;
}

static __always_inline void list_del(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    entry->next = entry;
    entry->prev = entry;
}

static __always_inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
    list_entry((ptr)->prev, type, member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

#define list_for_each_entry(pos, head, member)                                      \
    for (struct list_head *_node = (head)->next;                                    \
         _node != (head) && ((pos) = list_entry(_node, typeof(*(pos)), member), 1); \
         _node = _node->next)

/* Red-black tree node */
struct rb_node {
    unsigned long __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
} __aligned(sizeof(long));

struct rb_root {
    struct rb_node *rb_node;
};

#define RB_ROOT             (struct rb_root) { NULL }

void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **rb_link);
void rb_insert_color(struct rb_node *node, struct rb_root *root);
void rb_erase(struct rb_node *node, struct rb_root *root);
struct rb_node *rb_first(const struct rb_root *root);
struct rb_node *rb_last(const struct rb_root *root);
struct rb_node *rb_next(const struct rb_node *node);
struct rb_node *rb_prev(const struct rb_node *node);

/* Wait queue */
struct wait_queue_head {
    spinlock_t lock;
    struct list_head head;
};

typedef struct wait_queue_head wait_queue_head_t;

#define WAIT_QUEUE_HEAD_INIT(name) {                            \
.lock = SPINLOCK_INIT,                                      \
.head = LIST_HEAD_INIT((name).head) }

/* Function result types */
typedef enum {
    RESULT_OK = 0,
    RESULT_ERROR = -1,
    RESULT_NOMEM = -2,
    RESULT_INVAL = -3,
    RESULT_BUSY = -4,
    RESULT_TIMEOUT = -5,
    RESULT_PERM = -6,
    RESULT_NOENT = -7,
    RESULT_EXIST = -8,
    RESULT_IO = -9,
} result_t;

#endif /* _OBELISK_TYPES_H */
