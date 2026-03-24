/*
 * Obelisk OS - AxiomFS Block Cache
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <axiomfs/axiomfs.h>
#include <mm/kmalloc.h>

/* Block cache entry */
struct block_cache_entry {
    uint64_t block_num;
    void *data;
    bool dirty;
    uint64_t last_access;
    struct list_head list;
    struct rb_node node;
};

/* Block cache */
struct block_cache {
    struct rb_root tree;
    struct list_head lru;
    size_t count;
    size_t max_entries;
    size_t block_size;
    spinlock_t lock;
    uint64_t hits;
    uint64_t misses;
};

/* ==========================================================================
 * Block Cache Implementation
 * ========================================================================== */

struct block_cache *block_cache_create(size_t max_entries, size_t block_size) {
    struct block_cache *cache = kzalloc(sizeof(struct block_cache));
    if (!cache) return NULL;
    
    cache->tree = RB_ROOT;
    INIT_LIST_HEAD(&cache->lru);
    cache->count = 0;
    cache->max_entries = max_entries;
    cache->block_size = block_size;
    cache->lock = (spinlock_t)SPINLOCK_INIT;
    cache->hits = 0;
    cache->misses = 0;
    
    return cache;
}

void block_cache_destroy(struct block_cache *cache) {
    if (!cache) return;
    
    struct list_head *pos, *tmp;
    list_for_each_safe(pos, tmp, &cache->lru) {
        struct block_cache_entry *entry = 
            list_entry(pos, struct block_cache_entry, list);
        list_del(&entry->list);
        kfree(entry->data);
        kfree(entry);
    }
    
    kfree(cache);
}

void *block_cache_get(struct block_cache *cache, uint64_t block_num) {
    struct rb_node *node = cache->tree.rb_node;
    
    while (node) {
        struct block_cache_entry *entry = 
            container_of(node, struct block_cache_entry, node);
        
        if (block_num < entry->block_num) {
            node = node->rb_left;
        } else if (block_num > entry->block_num) {
            node = node->rb_right;
        } else {
            /* Found */
            entry->last_access = get_ticks();
            list_del(&entry->list);
            list_add(&entry->list, &cache->lru);
            cache->hits++;
            return entry->data;
        }
    }
    
    cache->misses++;
    return NULL;
}

int block_cache_put(struct block_cache *cache, uint64_t block_num, 
                    const void *data, bool dirty) {
    /* Evict if full */
    while (cache->count >= cache->max_entries) {
        struct block_cache_entry *oldest = 
            list_last_entry(&cache->lru, struct block_cache_entry, list);
        
        /* Write back if dirty */
        if (oldest->dirty) {
            /* TODO: Write to disk */
        }
        
        rb_erase(&oldest->node, &cache->tree);
        list_del(&oldest->list);
        kfree(oldest->data);
        kfree(oldest);
        cache->count--;
    }
    
    /* Create new entry */
    struct block_cache_entry *entry = kzalloc(sizeof(struct block_cache_entry));
    if (!entry) return -ENOMEM;
    
    entry->data = kmalloc(cache->block_size);
    if (!entry->data) {
        kfree(entry);
        return -ENOMEM;
    }
    
    entry->block_num = block_num;
    memcpy(entry->data, data, cache->block_size);
    entry->dirty = dirty;
    entry->last_access = get_ticks();
    
    /* Insert into tree */
    struct rb_node **new = &cache->tree.rb_node;
    struct rb_node *parent = NULL;
    
    while (*new) {
        struct block_cache_entry *this = 
            container_of(*new, struct block_cache_entry, node);
        parent = *new;
        
        if (block_num < this->block_num) {
            new = &(*new)->rb_left;
        } else {
            new = &(*new)->rb_right;
        }
    }
    
    rb_link_node(&entry->node, parent, new);
    rb_insert_color(&entry->node, &cache->tree);
    
    list_add(&entry->list, &cache->lru);
    cache->count++;
    
    return 0;
}

void block_cache_invalidate(struct block_cache *cache, uint64_t block_num) {
    struct rb_node *node = cache->tree.rb_node;
    
    while (node) {
        struct block_cache_entry *entry = 
            container_of(node, struct block_cache_entry, node);
        
        if (block_num < entry->block_num) {
            node = node->rb_left;
        } else if (block_num > entry->block_num) {
            node = node->rb_right;
        } else {
            rb_erase(&entry->node, &cache->tree);
            list_del(&entry->list);
            kfree(entry->data);
            kfree(entry);
            cache->count--;
            return;
        }
    }
}

void block_cache_sync(struct block_cache *cache) {
    struct list_head *pos;
    
    list_for_each(pos, &cache->lru) {
        struct block_cache_entry *entry = 
            list_entry(pos, struct block_cache_entry, list);
        
        if (entry->dirty) {
            /* TODO: Write to disk */
            entry->dirty = false;
        }
    }
}

/* ==========================================================================
 * Block I/O (Stub for RAM-based FS)
 * ========================================================================== */

int axiomfs_read_block(struct super_block *sb, uint64_t block, void *buf) {
    /* For RAM-based filesystem, blocks are in memory */
    (void)sb;
    (void)block;
    (void)buf;
    return 0;
}

int axiomfs_write_block(struct super_block *sb, uint64_t block, const void *buf) {
    (void)sb;
    (void)block;
    (void)buf;
    return 0;
}

/* ==========================================================================
 * Block Allocation (Simple)
 * ========================================================================== */

static uint64_t next_free_block = 100;

uint64_t axiomfs_alloc_block(struct super_block *sb) {
    struct axiomfs_sb_info *sbi = sb->s_fs_info;
    
    if (sbi->s_sb->s_free_blocks == 0) {
        return 0;
    }
    
    sbi->s_sb->s_free_blocks--;
    return next_free_block++;
}

void axiomfs_free_block(struct super_block *sb, uint64_t block) {
    struct axiomfs_sb_info *sbi = sb->s_fs_info;
    
    /* Simple implementation - just increment free count */
    sbi->s_sb->s_free_blocks++;
    (void)block;
}