/*
 * Obelisk OS - Message Queue Implementation
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <ipc/msgqueue.h>
#include <mm/kmalloc.h>
#include <proc/process.h>

/* Maximum queues */
#define MAX_QUEUES      256

/* Queue table */
static struct ipc_queue *queue_table[MAX_QUEUES];
static uint32_t next_queue_id = 1;

/* Message cache */
static struct kmem_cache *message_cache;

/* ==========================================================================
 * Queue Management
 * ========================================================================== */

int msgqueue_create(uint32_t key, mode_t mode) {
    struct ipc_queue *queue;
    int qid;
    
    /* Check for existing queue with same key */
    if (key != 0) {
        for (int i = 0; i < MAX_QUEUES; i++) {
            if (queue_table[i] && queue_table[i]->key == key) {
                return -EEXIST;
            }
        }
    }
    
    /* Find free slot */
    qid = -1;
    for (int i = 0; i < MAX_QUEUES; i++) {
        if (queue_table[i] == NULL) {
            qid = i;
            break;
        }
    }
    
    if (qid < 0) {
        return -ENOSPC;
    }
    
    /* Allocate queue */
    queue = kzalloc(sizeof(struct ipc_queue));
    if (!queue) {
        return -ENOMEM;
    }
    
    queue->id = next_queue_id++;
    queue->key = key;
    queue->uid = current ? current->cred->uid : 0;
    queue->gid = current ? current->cred->gid : 0;
    queue->mode = mode;
    
    queue->max_messages = 256;
    queue->max_message_size = 8192;
    queue->current_messages = 0;
    queue->current_bytes = 0;
    
    INIT_LIST_HEAD(&queue->messages);
    queue->readers = (wait_queue_head_t)WAIT_QUEUE_HEAD_INIT(queue->readers);
    queue->writers = (wait_queue_head_t)WAIT_QUEUE_HEAD_INIT(queue->writers);
    queue->lock = (spinlock_t)SPINLOCK_INIT;
    
    queue_table[qid] = queue;
    
    return qid;
}

int msgqueue_get(uint32_t key) {
    for (int i = 0; i < MAX_QUEUES; i++) {
        if (queue_table[i] && queue_table[i]->key == key) {
            return i;
        }
    }
    return -ENOENT;
}

int msgqueue_destroy(int qid) {
    struct ipc_queue *queue;
    
    if (qid < 0 || qid >= MAX_QUEUES) {
        return -EINVAL;
    }
    
    queue = queue_table[qid];
    if (!queue) {
        return -EINVAL;
    }
    
    /* Free all messages */
    struct list_head *pos, *tmp;
    list_for_each_safe(pos, tmp, &queue->messages) {
        struct ipc_message *msg = list_entry(pos, struct ipc_message, list);
        list_del(pos);
        kfree(msg);
    }
    
    queue_table[qid] = NULL;
    kfree(queue);
    
    return 0;
}

/* ==========================================================================
 * Message Operations
 * ========================================================================== */

int msgqueue_send(int qid, const void *data, size_t size, uint32_t type, int flags) {
    struct ipc_queue *queue;
    struct ipc_message *msg;
    
    if (qid < 0 || qid >= MAX_QUEUES) {
        return -EINVAL;
    }
    
    queue = queue_table[qid];
    if (!queue) {
        return -EINVAL;
    }
    
    if (size > queue->max_message_size) {
        return -EMSGSIZE;
    }
    
    /* Check if queue is full */
    while (queue->current_messages >= queue->max_messages) {
        if (flags & MSG_NOWAIT) {
            return -EAGAIN;
        }
        /* TODO: Wait on writers queue */
        return -EAGAIN;
    }
    
    /* Allocate message */
    msg = kmalloc(sizeof(struct ipc_message) + size);
    if (!msg) {
        return -ENOMEM;
    }
    
    msg->type = type;
    msg->size = size;
    msg->sender = current ? current->pid : 0;
    msg->flags = 0;
    msg->timestamp = get_ticks();
    memcpy(msg->payload, data, size);
    
    /* Add to queue */
    list_add_tail(&msg->list, &queue->messages);
    queue->current_messages++;
    queue->current_bytes += size;
    queue->send_time = msg->timestamp;
    
    /* Wake up readers */
    /* TODO: Wake readers queue */
    
    return 0;
}

ssize_t msgqueue_recv(int qid, void *data, size_t size, uint32_t type, int flags) {
    struct ipc_queue *queue;
    struct ipc_message *msg = NULL;
    struct list_head *pos;
    
    if (qid < 0 || qid >= MAX_QUEUES) {
        return -EINVAL;
    }
    
    queue = queue_table[qid];
    if (!queue) {
        return -EINVAL;
    }
    
    /* Find matching message */
    while (1) {
        list_for_each(pos, &queue->messages) {
            struct ipc_message *m = list_entry(pos, struct ipc_message, list);
            
            if (type == 0 || m->type == type) {
                msg = m;
                break;
            } else if ((flags & MSG_EXCEPT) && m->type != type) {
                msg = m;
                break;
            }
        }
        
        if (msg) {
            break;
        }
        
        if (flags & MSG_NOWAIT) {
            return -EAGAIN;
        }
        
        /* TODO: Wait on readers queue */
        return -EAGAIN;
    }
    
    /* Copy message */
    size_t copy_size = MIN(size, msg->size);
    if (copy_size < msg->size && !(flags & MSG_NOERROR)) {
        return -E2BIG;
    }
    
    memcpy(data, msg->payload, copy_size);
    
    /* Remove from queue */
    list_del(&msg->list);
    queue->current_messages--;
    queue->current_bytes -= msg->size;
    queue->recv_time = get_ticks();
    
    ssize_t ret = copy_size;
    kfree(msg);
    
    /* Wake up writers */
    /* TODO: Wake writers queue */
    
    return ret;
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void msgqueue_init(void) {
    printk(KERN_INFO "Initializing message queues...\n");
    
    memset(queue_table, 0, sizeof(queue_table));
    
    message_cache = kmem_cache_create("ipc_message", 
                                      sizeof(struct ipc_message) + 256,
                                      sizeof(void *), 0, NULL);
    
    printk(KERN_INFO "Message queues initialized\n");
}

void ipc_init(void) {
    msgqueue_init();
}