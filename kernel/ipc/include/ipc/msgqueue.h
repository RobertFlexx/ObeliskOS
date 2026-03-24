/*
 * Obelisk OS - Message Queue Header
 * From Axioms, Order.
 */

#ifndef _IPC_MSGQUEUE_H
#define _IPC_MSGQUEUE_H

#include <obelisk/types.h>

/* Message flags */
#define MSG_NOERROR     0x1000
#define MSG_NOWAIT      0x2000
#define MSG_EXCEPT      0x4000

/* Message structure */
struct ipc_message {
    uint32_t type;              /* Message type */
    uint32_t size;              /* Payload size */
    pid_t sender;               /* Sender PID */
    uint32_t flags;             /* Message flags */
    uint64_t timestamp;         /* Message timestamp */
    struct list_head list;      /* Queue linkage */
    uint8_t payload[];          /* Variable-length payload */
};

/* Message queue */
struct ipc_queue {
    uint32_t id;                /* Queue ID */
    uint32_t key;               /* Queue key */
    uid_t uid;                  /* Owner UID */
    gid_t gid;                  /* Owner GID */
    mode_t mode;                /* Permissions */
    
    size_t max_messages;        /* Maximum messages */
    size_t max_message_size;    /* Maximum message size */
    size_t current_messages;    /* Current message count */
    size_t current_bytes;       /* Current total bytes */
    
    struct list_head messages;  /* Message list */
    
    wait_queue_head_t readers;  /* Waiting readers */
    wait_queue_head_t writers;  /* Waiting writers */
    
    spinlock_t lock;            /* Queue lock */
    
    uint64_t send_time;         /* Last send time */
    uint64_t recv_time;         /* Last receive time */
    
    struct list_head list;      /* Global queue list */
};

/* Queue operations */
void msgqueue_init(void);
int msgqueue_create(uint32_t key, mode_t mode);
int msgqueue_get(uint32_t key);
int msgqueue_destroy(int qid);

int msgqueue_send(int qid, const void *msg, size_t size, uint32_t type, int flags);
ssize_t msgqueue_recv(int qid, void *msg, size_t size, uint32_t type, int flags);

int msgqueue_stat(int qid, struct ipc_queue **info);

#endif /* _IPC_MSGQUEUE_H */