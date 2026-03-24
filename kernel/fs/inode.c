/*
 * Obelisk OS - Inode Operations
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <proc/process.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <mm/kmalloc.h>

/* Inode hash table */
static struct list_head inode_hashtable[INODE_HASH_SIZE];
static bool inode_cache_initialized = false;

/* Global inode list */
static LIST_HEAD(inode_in_use);
static LIST_HEAD(inode_unused);

/* Hash function */
static inline uint32_t inode_hash(struct super_block *sb, ino_t ino) {
    uint32_t hash = (uint32_t)(uintptr_t)sb ^ (uint32_t)ino;
    return hash & (INODE_HASH_SIZE - 1);
}

/* ==========================================================================
 * Inode Cache
 * ========================================================================== */

void inode_cache_init(void) {
    if (inode_cache_initialized) {
        return;
    }
    for (int i = 0; i < INODE_HASH_SIZE; i++) {
        INIT_LIST_HEAD(&inode_hashtable[i]);
    }
    inode_cache_initialized = true;
}

struct inode *inode_cache_lookup(struct super_block *sb, ino_t ino) {
    if (!inode_cache_initialized) {
        inode_cache_init();
    }
    uint32_t hash = inode_hash(sb, ino);
    struct inode *inode;
    
    list_for_each_entry(inode, &inode_hashtable[hash], i_list) {
        if (inode->i_sb == sb && inode->i_ino == ino) {
            ihold(inode);
            return inode;
        }
    }
    
    return NULL;
}

void inode_cache_insert(struct inode *inode) {
    if (!inode || !inode->i_sb) {
        return;
    }
    if (!inode_cache_initialized) {
        inode_cache_init();
    }
    uint32_t hash = inode_hash(inode->i_sb, inode->i_ino);
    list_add(&inode->i_list, &inode_hashtable[hash]);
}

void inode_cache_remove(struct inode *inode) {
    if (!inode) {
        return;
    }
    if (!inode_cache_initialized) {
        inode_cache_init();
    }
    list_del(&inode->i_list);
    INIT_LIST_HEAD(&inode->i_list);
}

/* ==========================================================================
 * Inode Allocation
 * ========================================================================== */

struct inode *new_inode(struct super_block *sb) {
    struct inode *inode;
    
    if (sb->s_op && sb->s_op->alloc_inode) {
        inode = sb->s_op->alloc_inode(sb);
    } else {
        inode = kmem_cache_zalloc(inode_cache);
    }
    
    if (!inode) {
        return NULL;
    }
    
    inode_init_once(inode);
    inode->i_sb = sb;
    inode->i_blksize = sb->s_blocksize;
    inode->i_flags = I_NEW;
    inode->i_count.counter = 1;
    
    return inode;
}

void free_inode(struct inode *inode) {
    if (!inode) return;
    
    if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->destroy_inode) {
        inode->i_sb->s_op->destroy_inode(inode);
    } else {
        kmem_cache_free(inode_cache, inode);
    }
}

void inode_init_once(struct inode *inode) {
    memset(inode, 0, sizeof(*inode));
    INIT_LIST_HEAD(&inode->i_list);
    INIT_LIST_HEAD(&inode->i_sb_list);
    INIT_LIST_HEAD(&inode->i_dentry);
    inode->i_lock = (spinlock_t)SPINLOCK_INIT;
    inode->i_count.counter = 0;
}

void inode_init_owner(struct inode *inode, const struct inode *dir, mode_t mode) {
    inode->i_uid = current ? current->cred->fsuid : 0;
    
    if (dir && (dir->i_mode & S_ISGID)) {
        inode->i_gid = dir->i_gid;
        if (S_ISDIR(mode)) {
            mode |= S_ISGID;
        }
    } else {
        inode->i_gid = current ? current->cred->fsgid : 0;
    }
    
    inode->i_mode = mode;
}

/* ==========================================================================
 * Reference Counting
 * ========================================================================== */

struct inode *igrab(struct inode *inode) {
    if (inode) {
        if (inode->i_count.counter > 0) {
            inode->i_count.counter++;
            return inode;
        }
    }
    return NULL;
}

void ihold(struct inode *inode) {
    if (inode) {
        inode->i_count.counter++;
    }
}

void iput(struct inode *inode) {
    if (!inode) return;
    
    if (--inode->i_count.counter == 0) {
        /* Last reference, free inode */
        inode_cache_remove(inode);
        
        if (inode->i_sb && inode->i_sb->s_op) {
            if (inode->i_sb->s_op->drop_inode) {
                inode->i_sb->s_op->drop_inode(inode);
            }
            if (inode->i_sb->s_op->delete_inode) {
                inode->i_sb->s_op->delete_inode(inode);
            }
        }
        
        free_inode(inode);
    }
}

/* ==========================================================================
 * Inode Locking
 * ========================================================================== */

void inode_lock(struct inode *inode) {
    /* Simple spinlock for now */
    while (inode->i_flags & I_LOCK) {
        pause();
    }
    inode->i_flags |= I_LOCK;
}

void inode_unlock(struct inode *inode) {
    inode->i_flags &= ~I_LOCK;
}

bool inode_trylock(struct inode *inode) {
    if (inode->i_flags & I_LOCK) {
        return false;
    }
    inode->i_flags |= I_LOCK;
    return true;
}

/* ==========================================================================
 * Inode Operations
 * ========================================================================== */

void mark_inode_dirty(struct inode *inode) {
    if (inode) {
        inode->i_flags |= I_DIRTY;
        
        if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->dirty_inode) {
            inode->i_sb->s_op->dirty_inode(inode);
        }
    }
}

void clear_inode(struct inode *inode) {
    inode->i_flags |= I_CLEAR;
}

struct inode *iget_locked(struct super_block *sb, ino_t ino) {
    struct inode *inode;
    
    /* Check cache first */
    inode = inode_cache_lookup(sb, ino);
    if (inode) {
        return inode;
    }
    
    /* Allocate new inode */
    inode = new_inode(sb);
    if (!inode) {
        return NULL;
    }
    
    inode->i_ino = ino;
    inode_cache_insert(inode);
    
    return inode;
}

void unlock_new_inode(struct inode *inode) {
    inode->i_flags &= ~I_NEW;
}

/* ==========================================================================
 * Generic Operations
 * ========================================================================== */

int generic_permission(struct inode *inode, int mask) {
    uid_t uid = current ? current->cred->fsuid : 0;
    gid_t gid = current ? current->cred->fsgid : 0;
    mode_t mode = inode->i_mode;
    
    /* Root can do anything */
    if (uid == 0) {
        return 0;
    }
    
    /* Check owner permissions */
    if (uid == inode->i_uid) {
        mode >>= 6;
    }
    /* Check group permissions */
    else if (gid == inode->i_gid) {
        mode >>= 3;
    }
    /* Check other permissions */
    
    if ((mask & MAY_READ) && !(mode & S_IROTH)) {
        return -EACCES;
    }
    if ((mask & MAY_WRITE) && !(mode & S_IWOTH)) {
        return -EACCES;
    }
    if ((mask & MAY_EXEC) && !(mode & S_IXOTH)) {
        return -EACCES;
    }
    
    return 0;
}

int generic_getattr(const struct dentry *dentry, struct kstat *stat) {
    struct inode *inode = dentry->d_inode;
    
    stat->dev = inode->i_sb ? inode->i_sb->s_dev : 0;
    stat->ino = inode->i_ino;
    stat->mode = inode->i_mode;
    stat->nlink = inode->i_nlink;
    stat->uid = inode->i_uid;
    stat->gid = inode->i_gid;
    stat->rdev = inode->i_rdev;
    stat->size = inode->i_size;
    stat->atime = inode->i_atime;
    stat->mtime = inode->i_mtime;
    stat->ctime = inode->i_ctime;
    stat->blksize = inode->i_blksize;
    stat->blocks = inode->i_blocks;
    
    return 0;
}