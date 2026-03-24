/*
 * Obelisk OS - Axiom Daemon Channel
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <ipc/axiomd.h>
#include <ipc/msgqueue.h>
#include <mm/kmalloc.h>

/* Global channel */
static struct axiomd_channel channel;

/* Queue IDs */
static int kernel_to_daemon_qid = -1;
static int daemon_to_kernel_qid = -1;

/* Daemon PID */
static pid_t daemon_pid = 0;

/* ==========================================================================
 * Channel Management
 * ========================================================================== */

void axiomd_channel_init(void) {
    printk(KERN_INFO "Initializing axiomd channel...\n");
    
    memset(&channel, 0, sizeof(channel));
    channel.state = AXIOMD_STATE_DISCONNECTED;
    INIT_LIST_HEAD(&channel.pending_queries);
    channel.lock = (spinlock_t)SPINLOCK_INIT;
    channel.next_query_id = 1;
    channel.daemon_alive = false;
    
    /* Create queues */
    kernel_to_daemon_qid = msgqueue_create(0xA710D001, 0600);
    daemon_to_kernel_qid = msgqueue_create(0xA710D002, 0600);
    
    if (kernel_to_daemon_qid >= 0 && daemon_to_kernel_qid >= 0) {
        printk(KERN_INFO "axiomd channel initialized (queues: %d, %d)\n",
               kernel_to_daemon_qid, daemon_to_kernel_qid);
    } else {
        printk(KERN_WARNING "axiomd channel: Failed to create queues\n");
    }
}

int axiomd_connect(void) {
    if (channel.state == AXIOMD_STATE_CONNECTED) {
        return 0;
    }
    
    channel.state = AXIOMD_STATE_CONNECTING;
    
    /* Wait for daemon to register */
    /* For now, just check if daemon is registered */
    if (daemon_pid > 0) {
        channel.state = AXIOMD_STATE_CONNECTED;
        channel.daemon_alive = true;
        return 0;
    }
    
    return -EAXIOMD;
}

void axiomd_disconnect(void) {
    channel.state = AXIOMD_STATE_DISCONNECTED;
    channel.daemon_alive = false;
    
    /* Clear pending queries */
    struct list_head *pos, *tmp;
    list_for_each_safe(pos, tmp, &channel.pending_queries) {
        struct axiomd_pending_query *pq = 
            list_entry(pos, struct axiomd_pending_query, list);
        list_del(&pq->list);
        kfree(pq);
    }
}

bool axiomd_is_connected(void) {
    return channel.state == AXIOMD_STATE_CONNECTED && channel.daemon_alive;
}

/* ==========================================================================
 * Query Operations
 * ========================================================================== */

int axiomd_send_query(const void *query, size_t size) {
    if (!axiomd_is_connected()) {
        return -EAXIOMD;
    }
    
    return msgqueue_send(kernel_to_daemon_qid, query, size, 
                         AXIOMD_MSG_QUERY, MSG_NOWAIT);
}

int axiomd_recv_response(void *response, size_t size, uint32_t timeout_ms) {
    if (!axiomd_is_connected()) {
        return -EAXIOMD;
    }
    
    uint64_t start = get_ticks();
    uint64_t timeout = timeout_ms;
    
    while (get_ticks() - start < timeout) {
        ssize_t ret = msgqueue_recv(daemon_to_kernel_qid, response, size,
                                    AXIOMD_MSG_RESPONSE, MSG_NOWAIT);
        if (ret > 0) {
            return ret;
        }
        
        if (ret != -EAGAIN) {
            return ret;
        }
        
        /* Brief delay */
        for (volatile int i = 0; i < 10000; i++);
    }
    
    return -ETIMEDOUT;
}

int axiomd_query_sync(const void *query, size_t query_size,
                      void *response, size_t response_size,
                      uint32_t timeout_ms) {
    int ret;
    
    ret = axiomd_send_query(query, query_size);
    if (ret < 0) {
        return ret;
    }
    
    return axiomd_recv_response(response, response_size, timeout_ms);
}

/* ==========================================================================
 * Daemon Registration
 * ========================================================================== */

int axiomd_daemon_register(pid_t pid) {
    printk(KERN_INFO "axiomd: Daemon registered (pid=%d)\n", pid);
    
    daemon_pid = pid;
    channel.state = AXIOMD_STATE_CONNECTED;
    channel.daemon_alive = true;
    channel.last_pong = get_ticks();
    
    return 0;
}

int axiomd_daemon_unregister(void) {
    printk(KERN_INFO "axiomd: Daemon unregistered\n");
    
    daemon_pid = 0;
    axiomd_disconnect();
    
    return 0;
}

/* ==========================================================================
 * Health Check
 * ========================================================================== */

int axiomd_ping(void) {
    if (!axiomd_is_connected()) {
        return -EAXIOMD;
    }
    
    uint8_t ping_msg = AXIOMD_MSG_PING;
    int ret = msgqueue_send(kernel_to_daemon_qid, &ping_msg, 1, 
                            AXIOMD_MSG_PING, MSG_NOWAIT);
    if (ret < 0) {
        return ret;
    }
    
    channel.last_ping = get_ticks();
    
    /* Wait for pong */
    uint8_t pong_msg;
    ret = axiomd_recv_response(&pong_msg, 1, 100);
    if (ret < 0) {
        channel.daemon_alive = false;
        return ret;
    }
    
    channel.last_pong = get_ticks();
    return 0;
}