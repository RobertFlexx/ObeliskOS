/*
 * Obelisk OS - AxiomFS Policy Interface
 * From Axioms, Order.
 */

#ifndef _AXIOMFS_POLICY_H
#define _AXIOMFS_POLICY_H

#include <obelisk/types.h>
#include <fs/vfs.h>

/* Policy operations */
#define POLICY_OP_READ          1
#define POLICY_OP_WRITE         2
#define POLICY_OP_EXECUTE       3
#define POLICY_OP_DELETE        4
#define POLICY_OP_CREATE        5
#define POLICY_OP_MODIFY_ATTR   6

/* Policy results */
#define POLICY_ALLOW            0
#define POLICY_DENY             1
#define POLICY_DEFAULT          2   /* Fall back to mode bits */
#define POLICY_ERROR            3
#define POLICY_TIMEOUT          4

/* Policy query */
struct policy_query {
    uint32_t query_id;              /* Query identifier */
    uint32_t operation;             /* Operation type */
    uid_t uid;                      /* User ID */
    gid_t gid;                      /* Group ID */
    ino_t inode;                    /* Inode number */
    char path[PATH_MAX];            /* File path */
    uint32_t flags;                 /* Query flags */
};

/* Policy response */
struct policy_response {
    uint32_t query_id;              /* Query identifier */
    int result;                     /* Policy decision */
    uint32_t flags;                 /* Response flags */
    char reason[256];               /* Denial reason (for logging) */
};

/* Policy cache entry */
struct policy_cache_entry {
    uint64_t hash;                  /* Cache key hash */
    ino_t inode;                    /* Inode number */
    uid_t uid;                      /* User ID */
    gid_t gid;                      /* Group ID */
    uint32_t operation;             /* Operation type */
    int result;                     /* Cached result */
    uint64_t expiry;                /* Cache expiry time */
    struct rb_node node;            /* Red-black tree node */
    struct list_head lru;           /* LRU list */
};

/* Policy cache */
struct policy_cache {
    struct rb_root tree;            /* RB-tree for lookups */
    struct list_head lru;           /* LRU list */
    size_t count;                   /* Entry count */
    size_t max_entries;             /* Maximum entries */
    uint64_t default_ttl;           /* Default TTL (ms) */
    spinlock_t lock;                /* Cache lock */
    uint64_t hits;                  /* Cache hits */
    uint64_t misses;                /* Cache misses */
};

/* Initialize policy subsystem */
void policy_init(void);

/* Policy query functions */
int policy_check_access(uid_t uid, gid_t gid, ino_t inode,
                        const char *path, uint32_t operation);
int policy_get_allocation(ino_t inode, const char *path, uint64_t *preferred_block);
int policy_get_inherited_mode(ino_t parent_inode, const char *parent_path,
                              mode_t *mode, uid_t *uid, gid_t *gid);

/* Policy cache functions */
struct policy_cache *policy_cache_create(size_t max_entries, uint64_t ttl_ms);
void policy_cache_destroy(struct policy_cache *cache);
int policy_cache_lookup(struct policy_cache *cache, uid_t uid, gid_t gid,
                        ino_t inode, uint32_t operation);
void policy_cache_insert(struct policy_cache *cache, uid_t uid, gid_t gid,
                         ino_t inode, uint32_t operation, int result);
void policy_cache_invalidate(struct policy_cache *cache, ino_t inode);
void policy_cache_flush(struct policy_cache *cache);

/* Daemon communication */
bool policy_daemon_available(void);
int policy_daemon_query(struct policy_query *query, struct policy_response *response);

#endif /* _AXIOMFS_POLICY_H */