/*
 * Obelisk OS - Axiom Daemon IPC Header
 * From Axioms, Order.
 */

#ifndef _IPC_AXIOMD_H
#define _IPC_AXIOMD_H

#include <obelisk/types.h>
#include <axiomfs/policy.h>

/* Axiomd message types */
#define AXIOMD_MSG_QUERY        1
#define AXIOMD_MSG_RESPONSE     2
#define AXIOMD_MSG_REGISTER     3
#define AXIOMD_MSG_UNREGISTER   4
#define AXIOMD_MSG_INVALIDATE   5
#define AXIOMD_MSG_PING         6
#define AXIOMD_MSG_PONG         7

/* Axiomd channel state */
typedef enum {
    AXIOMD_STATE_DISCONNECTED,
    AXIOMD_STATE_CONNECTING,
    AXIOMD_STATE_CONNECTED,
    AXIOMD_STATE_ERROR
} axiomd_state_t;

/* Pending query */
struct axiomd_pending_query {
    uint32_t query_id;
    uint64_t timestamp;
    uint32_t timeout_ms;
    bool completed;
    int result;
    void *response_buf;
    size_t response_size;
    struct list_head list;
};

/* Axiomd channel */
struct axiomd_channel {
    axiomd_state_t state;
    struct ipc_queue *kernel_to_daemon;
    struct ipc_queue *daemon_to_kernel;
    uint32_t next_query_id;
    struct list_head pending_queries;
    spinlock_t lock;
    bool daemon_alive;
    uint64_t last_ping;
    uint64_t last_pong;
};

/* Channel operations */
void axiomd_channel_init(void);
int axiomd_connect(void);
void axiomd_disconnect(void);
bool axiomd_is_connected(void);

/* Query operations */
int axiomd_send_query(const void *query, size_t size);
int axiomd_recv_response(void *response, size_t size, uint32_t timeout_ms);
int axiomd_query_sync(const void *query, size_t query_size,
                      void *response, size_t response_size,
                      uint32_t timeout_ms);

/* Daemon registration (called by axiomd) */
int axiomd_daemon_register(pid_t pid);
int axiomd_daemon_unregister(void);

/* Health check */
int axiomd_ping(void);

#endif /* _IPC_AXIOMD_H */