/*
 * Obelisk OS - AxiomFS Policy IPC
 * From Axioms, Order.
 *
 * Handles communication with the Prolog policy daemon (axiomd).
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <axiomfs/axiomfs.h>
#include <axiomfs/policy.h>
#include <ipc/axiomd.h>
#include <mm/kmalloc.h>

/* Policy cache */
static struct policy_cache *global_policy_cache = NULL;

/* Daemon state */
static bool daemon_available = false;
static uint32_t next_query_id = 1;

/* Default timeout (ms) */
#define POLICY_TIMEOUT_MS   100

/* ==========================================================================
 * Policy Cache Implementation
 * ========================================================================== */

static uint64_t policy_cache_hash(uid_t uid, gid_t gid, ino_t inode, uint32_t op) {
    uint64_t hash = uid;
    hash = hash * 31 + gid;
    hash = hash * 31 + inode;
    hash = hash * 31 + op;
    return hash;
}

struct policy_cache *policy_cache_create(size_t max_entries, uint64_t ttl_ms) {
    struct policy_cache *cache = kzalloc(sizeof(struct policy_cache));
    if (!cache) return NULL;
    
    cache->tree = RB_ROOT;
    INIT_LIST_HEAD(&cache->lru);
    cache->count = 0;
    cache->max_entries = max_entries;
    cache->default_ttl = ttl_ms;
    cache->lock = (spinlock_t)SPINLOCK_INIT;
    cache->hits = 0;
    cache->misses = 0;
    
    return cache;
}

void policy_cache_destroy(struct policy_cache *cache) {
    if (!cache) return;
    
    /* Free all entries */
    struct list_head *pos, *tmp;
    list_for_each_safe(pos, tmp, &cache->lru) {
        struct policy_cache_entry *entry = 
            list_entry(pos, struct policy_cache_entry, lru);
        list_del(&entry->lru);
        kfree(entry);
    }
    
    kfree(cache);
}

int policy_cache_lookup(struct policy_cache *cache, uid_t uid, gid_t gid,
                        ino_t inode, uint32_t operation) {
    if (!cache) return -1;
    
    uint64_t hash = policy_cache_hash(uid, gid, inode, operation);
    uint64_t now = get_ticks();
    
    /* Search in RB-tree */
    struct rb_node *node = cache->tree.rb_node;
    
    while (node) {
        struct policy_cache_entry *entry = 
            container_of(node, struct policy_cache_entry, node);
        
        if (hash < entry->hash) {
            node = node->rb_left;
        } else if (hash > entry->hash) {
            node = node->rb_right;
        } else {
            /* Found - check if still valid */
            if (entry->uid == uid && entry->gid == gid &&
                entry->inode == inode && entry->operation == operation) {
                
                if (now < entry->expiry) {
                    /* Move to front of LRU */
                    list_del(&entry->lru);
                    list_add(&entry->lru, &cache->lru);
                    cache->hits++;
                    return entry->result;
                }
                
                /* Expired */
                break;
            }
            /* Hash collision, continue searching */
            node = node->rb_right;
        }
    }
    
    cache->misses++;
    return -1;
}

void policy_cache_insert(struct policy_cache *cache, uid_t uid, gid_t gid,
                         ino_t inode, uint32_t operation, int result) {
    if (!cache) return;
    
    /* Evict if full */
    while (cache->count >= cache->max_entries) {
        struct policy_cache_entry *oldest = 
            list_last_entry(&cache->lru, struct policy_cache_entry, lru);
        rb_erase(&oldest->node, &cache->tree);
        list_del(&oldest->lru);
        kfree(oldest);
        cache->count--;
    }
    
    /* Create new entry */
    struct policy_cache_entry *entry = kzalloc(sizeof(struct policy_cache_entry));
    if (!entry) return;
    
    entry->hash = policy_cache_hash(uid, gid, inode, operation);
    entry->uid = uid;
    entry->gid = gid;
    entry->inode = inode;
    entry->operation = operation;
    entry->result = result;
    entry->expiry = get_ticks() + cache->default_ttl;
    
    /* Insert into RB-tree */
    struct rb_node **new = &cache->tree.rb_node;
    struct rb_node *parent = NULL;
    
    while (*new) {
        struct policy_cache_entry *this = 
            container_of(*new, struct policy_cache_entry, node);
        parent = *new;
        
        if (entry->hash < this->hash) {
            new = &(*new)->rb_left;
        } else {
            new = &(*new)->rb_right;
        }
    }
    
    rb_link_node(&entry->node, parent, new);
    rb_insert_color(&entry->node, &cache->tree);
    
    /* Add to LRU */
    list_add(&entry->lru, &cache->lru);
    cache->count++;
}

void policy_cache_invalidate(struct policy_cache *cache, ino_t inode) {
    if (!cache) return;
    
    struct list_head *pos, *tmp;
    list_for_each_safe(pos, tmp, &cache->lru) {
        struct policy_cache_entry *entry = 
            list_entry(pos, struct policy_cache_entry, lru);
        
        if (entry->inode == inode) {
            rb_erase(&entry->node, &cache->tree);
            list_del(&entry->lru);
            kfree(entry);
            cache->count--;
        }
    }
}

void policy_cache_flush(struct policy_cache *cache) {
    if (!cache) return;
    
    struct list_head *pos, *tmp;
    list_for_each_safe(pos, tmp, &cache->lru) {
        struct policy_cache_entry *entry = 
            list_entry(pos, struct policy_cache_entry, lru);
        rb_erase(&entry->node, &cache->tree);
        list_del(&entry->lru);
        kfree(entry);
    }
    
    cache->count = 0;
    cache->tree = RB_ROOT;
}

/* ==========================================================================
 * Policy Query Functions
 * ========================================================================== */

static void policy_refresh_daemon_state(void) {
    daemon_available = axiomd_is_connected();
}

static int policy_fallback_result(uint32_t operation) {
    switch (operation) {
        case POLICY_OP_READ:
        case POLICY_OP_EXECUTE:
            return POLICY_DEFAULT;
        case POLICY_OP_WRITE:
        case POLICY_OP_DELETE:
        case POLICY_OP_CREATE:
        case POLICY_OP_MODIFY_ATTR:
        default:
            return POLICY_DENY;
    }
}

bool policy_daemon_available(void) {
    policy_refresh_daemon_state();
    return daemon_available;
}

int policy_daemon_query(struct policy_query *query, struct policy_response *response) {
    policy_refresh_daemon_state();
    if (!daemon_available) {
        return -EAXIOMD;
    }
    
    /* Send query via IPC */
    int ret = axiomd_send_query(query, sizeof(*query));
    if (ret < 0) {
        return ret;
    }
    
    /* Wait for response with timeout */
    ret = axiomd_recv_response(response, sizeof(*response), POLICY_TIMEOUT_MS);
    if (ret < 0) {
        if (ret == -ETIMEDOUT) {
            return -EPOLICYTIMEOUT;
        }
        return ret;
    }
    
    if (response->query_id != query->query_id) {
        return -EINVAL;
    }

    return 0;
}

int policy_check_access(uid_t uid, gid_t gid, ino_t inode,
                        const char *path, uint32_t operation) {
    int cached;
    
    /* Check cache first */
    cached = policy_cache_lookup(global_policy_cache, uid, gid, inode, operation);
    if (cached >= 0) {
        return cached;
    }
    
    /* If daemon not available, use safety-biased fallback policy */
    if (!daemon_available) {
        return policy_fallback_result(operation);
    }
    
    /* Build query */
    struct policy_query query;
    struct policy_response response;
    
    query.query_id = next_query_id++;
    query.operation = operation;
    query.uid = uid;
    query.gid = gid;
    query.inode = inode;
    strncpy(query.path, path, sizeof(query.path) - 1);
    query.path[sizeof(query.path) - 1] = '\0';
    query.flags = 0;
    
    /* Send query to daemon */
    int ret = policy_daemon_query(&query, &response);
    if (ret < 0) {
        /* On daemon/IPC error, degrade safely for mutating operations. */
        return policy_fallback_result(operation);
    }
    
    /* Cache result */
    policy_cache_insert(global_policy_cache, uid, gid, inode, 
                        operation, response.result);
    
    return response.result;
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void policy_init(void) {
    printk(KERN_INFO "AxiomFS: Initializing policy subsystem...\n");
    
    /* Create policy cache */
    global_policy_cache = policy_cache_create(
        AXIOMFS_CACHE_MAX_ENTRIES,
        AXIOMFS_POLICY_TIMEOUT_MS
    );
    
    if (!global_policy_cache) {
        printk(KERN_WARNING "AxiomFS: Failed to create policy cache\n");
    }
    
    policy_refresh_daemon_state();
    
    printk(KERN_INFO "AxiomFS: Policy subsystem initialized (daemon=%s)\n",
           daemon_available ? "yes" : "no");
}

int policy_get_allocation(ino_t inode, const char *path, uint64_t *preferred_block) {
    uint64_t hash = inode;
    const char *p = path ? path : "";

    if (!preferred_block) {
        return -EINVAL;
    }

    while (*p) {
        hash = (hash * 131) + (uint8_t)(*p++);
    }

    *preferred_block = hash;
    return 0;
}

int policy_get_inherited_mode(ino_t parent_inode, const char *parent_path,
                              mode_t *mode, uid_t *uid, gid_t *gid) {
    (void)parent_inode;

    if (!mode || !uid || !gid) {
        return -EINVAL;
    }

    if (parent_path &&
        (strcmp(parent_path, "/tmp") == 0 || strcmp(parent_path, "/var/tmp") == 0)) {
        *mode = 0600;
        *uid = 0;
        *gid = 0;
        return 0;
    }

    *mode = 0644;
    *uid = 0;
    *gid = 0;
    return 0;
}