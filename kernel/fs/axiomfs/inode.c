/*
 * Obelisk OS - AxiomFS Inode Operations
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <proc/process.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <axiomfs/axiomfs.h>
#include <axiomfs/policy.h>
#include <mm/kmalloc.h>

/* Inode operations */
extern const struct inode_operations axiomfs_dir_inode_ops;
extern const struct inode_operations axiomfs_file_inode_ops;
extern const struct file_operations axiomfs_dir_ops;
extern const struct file_operations axiomfs_file_ops;

/* In-memory inode storage (for RAM-based filesystem) */
#define MAX_INODES  65536
static struct axiomfs_inode *inode_table[MAX_INODES];

/* ==========================================================================
 * Inode Allocation
 * ========================================================================== */

static struct axiomfs_inode *axiomfs_alloc_disk_inode(void) {
    struct axiomfs_inode *ai = kzalloc(sizeof(struct axiomfs_inode));
    return ai;
}

static void axiomfs_free_disk_inode(struct axiomfs_inode *ai) {
    kfree(ai);
}

/* ==========================================================================
 * Inode Read/Write
 * ========================================================================== */

struct inode *axiomfs_iget(struct super_block *sb, ino_t ino) {
    struct inode *inode;
    struct axiomfs_inode_info *info;
    struct axiomfs_inode *ai;
    
    if (ino >= MAX_INODES) {
        return ERR_PTR(-EINVAL);
    }
    
    /* Check if already in cache */
    inode = inode_cache_lookup(sb, ino);
    if (inode) {
        return inode;
    }
    
    /* Allocate new inode */
    inode = new_inode(sb);
    if (!inode) {
        return ERR_PTR(-ENOMEM);
    }
    
    /* Allocate inode info */
    info = kzalloc(sizeof(struct axiomfs_inode_info));
    if (!info) {
        iput(inode);
        return ERR_PTR(-ENOMEM);
    }
    
    /* Get or create disk inode */
    ai = inode_table[ino];
    if (!ai) {
        ai = axiomfs_alloc_disk_inode();
        if (!ai) {
            kfree(info);
            iput(inode);
            return ERR_PTR(-ENOMEM);
        }
        inode_table[ino] = ai;
    }
    
    info->ai_inode = ai;
    info->ai_block = 0;
    info->ai_policy_cached = false;
    
    /* Fill in VFS inode */
    inode->i_ino = ino;
    inode->i_mode = ai->i_mode;
    inode->i_uid = ai->i_uid;
    inode->i_gid = ai->i_gid;
    inode->i_size = ai->i_size;
    inode->i_atime = ai->i_atime;
    inode->i_mtime = ai->i_mtime;
    inode->i_ctime = ai->i_ctime;
    inode->i_nlink = ai->i_links_count;
    inode->i_blocks = ai->i_blocks;
    inode->i_private = info;
    
    /* Set operations based on file type */
    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &axiomfs_dir_inode_ops;
        inode->i_fop = &axiomfs_dir_ops;
    } else if (S_ISREG(inode->i_mode)) {
        inode->i_op = &axiomfs_file_inode_ops;
        inode->i_fop = &axiomfs_file_ops;
    } else if (S_ISLNK(inode->i_mode)) {
        /* TODO: Symlink operations */
    }
    
    /* Add to cache */
    inode_cache_insert(inode);
    unlock_new_inode(inode);
    
    return inode;
}

int axiomfs_write_inode(struct inode *inode, bool sync) {
    struct axiomfs_inode_info *info = inode->i_private;
    struct axiomfs_inode *ai;
    
    if (!info) {
        return -EINVAL;
    }
    
    ai = info->ai_inode;
    if (!ai) {
        return -EINVAL;
    }
    
    /* Update disk inode from VFS inode */
    ai->i_mode = inode->i_mode;
    ai->i_uid = inode->i_uid;
    ai->i_gid = inode->i_gid;
    ai->i_size = inode->i_size;
    ai->i_atime = inode->i_atime;
    ai->i_mtime = inode->i_mtime;
    ai->i_ctime = inode->i_ctime;
    ai->i_links_count = inode->i_nlink;
    ai->i_blocks = inode->i_blocks;
    
    /* For persistent storage, write to disk here */
    (void)sync;
    
    return 0;
}

void axiomfs_delete_inode(struct inode *inode) {
    if (!inode || !inode->i_sb) {
        return;
    }

    /* If the last link was removed, free the persistent inode backing now
     * that the VFS inode is being destroyed (i_count == 0). */
    if (inode->i_nlink == 0) {
        axiomfs_free_inode_num(inode->i_sb, inode->i_ino);
    }

    /* Always free inode-private metadata. */
    axiomfs_evict_inode(inode);
}

void axiomfs_evict_inode(struct inode *inode) {
    struct axiomfs_inode_info *info = inode->i_private;
    
    if (info) {
        /* Don't free the disk inode, it may still be needed */
        kfree(info);
        inode->i_private = NULL;
    }
    
    clear_inode(inode);
}

/* ==========================================================================
 * Inode Number Allocation
 * ========================================================================== */

uint64_t axiomfs_alloc_inode_num(struct super_block *sb) {
    struct axiomfs_sb_info *sbi = sb->s_fs_info;
    
    /* Simple linear search for free inode */
    for (uint64_t i = 2; i < MAX_INODES; i++) {
        if (inode_table[i] == NULL) {
            sbi->s_sb->s_free_inodes--;
            return i;
        }
    }
    
    return 0;  /* No free inodes */
}

void axiomfs_free_inode_num(struct super_block *sb, uint64_t ino) {
    struct axiomfs_sb_info *sbi = sb->s_fs_info;
    
    if (ino < MAX_INODES && inode_table[ino]) {
        axiomfs_free_disk_inode(inode_table[ino]);
        inode_table[ino] = NULL;
        sbi->s_sb->s_free_inodes++;
    }
}

/* ==========================================================================
 * Create New Inode
 * ========================================================================== */

static struct inode *axiomfs_new_inode(struct inode *dir, mode_t mode) {
    struct super_block *sb = dir->i_sb;
    struct inode *inode;
    struct axiomfs_inode_info *info;
    struct axiomfs_inode *ai;
    ino_t ino;
    
    /* Allocate inode number */
    ino = axiomfs_alloc_inode_num(sb);
    if (ino == 0) {
        return ERR_PTR(-ENOSPC);
    }
    
    /* Check policy for inheritance */
    if (policy_daemon_available()) {
        int ret = axiomfs_policy_inherit_permissions(dir, NULL);
        if (ret == POLICY_DENY) {
            axiomfs_free_inode_num(sb, ino);
            return ERR_PTR(-EPOLICYDENIED);
        }
    }
    
    /* Allocate VFS inode */
    inode = new_inode(sb);
    if (!inode) {
        axiomfs_free_inode_num(sb, ino);
        return ERR_PTR(-ENOMEM);
    }
    
    /* Allocate disk inode */
    ai = axiomfs_alloc_disk_inode();
    if (!ai) {
        iput(inode);
        axiomfs_free_inode_num(sb, ino);
        return ERR_PTR(-ENOMEM);
    }
    
    inode_table[ino] = ai;
    
    /* Allocate inode info */
    info = kzalloc(sizeof(struct axiomfs_inode_info));
    if (!info) {
        axiomfs_free_disk_inode(ai);
        inode_table[ino] = NULL;
        iput(inode);
        axiomfs_free_inode_num(sb, ino);
        return ERR_PTR(-ENOMEM);
    }
    
    info->ai_inode = ai;
    
    /* Initialize inode */
    inode->i_ino = ino;
    inode_init_owner(inode, dir, mode);
    inode->i_size = 0;
    inode->i_atime = inode->i_mtime = inode->i_ctime = get_ticks();
    inode->i_nlink = 1;
    inode->i_blocks = 0;
    inode->i_private = info;
    
    /* Copy to disk inode */
    ai->i_mode = inode->i_mode;
    ai->i_uid = inode->i_uid;
    ai->i_gid = inode->i_gid;
    ai->i_size = 0;
    ai->i_atime = ai->i_mtime = ai->i_ctime = inode->i_atime;
    ai->i_links_count = 1;
    ai->i_blocks = 0;
    
    /* Set operations */
    if (S_ISDIR(mode)) {
        inode->i_op = &axiomfs_dir_inode_ops;
        inode->i_fop = &axiomfs_dir_ops;
        inode->i_nlink = 2;
        ai->i_links_count = 2;
    } else if (S_ISREG(mode)) {
        inode->i_op = &axiomfs_file_inode_ops;
        inode->i_fop = &axiomfs_file_ops;
    }
    
    /* Add to cache */
    inode_cache_insert(inode);
    mark_inode_dirty(inode);
    
    return inode;
}

/* ==========================================================================
 * Directory Operations
 * ========================================================================== */

int axiomfs_create(struct inode *dir, struct dentry *dentry, mode_t mode, bool excl) {
    struct inode *inode;
    
    (void)excl;
    
    /* Check policy */
    if (policy_daemon_available()) {
        int result = policy_check_access(
            current ? current->cred->fsuid : 0,
            current ? current->cred->fsgid : 0,
            dir->i_ino,
            dentry->d_name,
            POLICY_OP_CREATE
        );
        
        if (result == POLICY_DENY) {
            return -EACCES;
        }
    }
    
    inode = axiomfs_new_inode(dir, mode | S_IFREG);
    if (IS_ERR(inode)) {
        return PTR_ERR(inode);
    }
    
    d_instantiate(dentry, inode);
    
    return 0;
}

int axiomfs_mkdir(struct inode *dir, struct dentry *dentry, mode_t mode) {
    struct inode *inode;
    
    /* Check policy */
    if (policy_daemon_available()) {
        int result = policy_check_access(
            current ? current->cred->fsuid : 0,
            current ? current->cred->fsgid : 0,
            dir->i_ino,
            dentry->d_name,
            POLICY_OP_CREATE
        );
        
        if (result == POLICY_DENY) {
            return -EACCES;
        }
    }
    
    inode = axiomfs_new_inode(dir, mode | S_IFDIR);
    if (IS_ERR(inode)) {
        return PTR_ERR(inode);
    }
    
    /* Increment parent link count */
    dir->i_nlink++;
    mark_inode_dirty(dir);
    
    d_instantiate(dentry, inode);
    
    return 0;
}

int axiomfs_rmdir(struct inode *dir, struct dentry *dentry) {
    struct inode *inode = dentry->d_inode;
    
    if (!inode) {
        return -ENOENT;
    }
    
    /* Check if directory is empty */
    if (!list_empty(&dentry->d_subdirs)) {
        return -ENOTEMPTY;
    }
    
    /* Check policy */
    if (policy_daemon_available()) {
        int result = policy_check_access(
            current ? current->cred->fsuid : 0,
            current ? current->cred->fsgid : 0,
            inode->i_ino,
            dentry->d_name,
            POLICY_OP_DELETE
        );
        
        if (result == POLICY_DENY) {
            return -EACCES;
        }
    }
    
    /* Remove directory */
    inode->i_nlink = 0;
    dir->i_nlink--;
    
    mark_inode_dirty(inode);
    mark_inode_dirty(dir);
    
    /* Remove directory entry from parent immediately so it disappears from
     * listings and path lookup, while keeping the inode alive for any open
     * references (file->f_dentry). */
    list_del(&dentry->d_child);
    
    return 0;
}

int axiomfs_unlink(struct inode *dir, struct dentry *dentry) {
    struct inode *inode = dentry->d_inode;
    
    if (!inode) {
        return -ENOENT;
    }
    
    /* Check policy */
    if (policy_daemon_available()) {
        int result = policy_check_access(
            current ? current->cred->fsuid : 0,
            current ? current->cred->fsgid : 0,
            inode->i_ino,
            dentry->d_name,
            POLICY_OP_DELETE
        );
        
        if (result == POLICY_DENY) {
            return -EACCES;
        }
    }
    
    inode->i_nlink--;
    mark_inode_dirty(inode);

    /* Unlink name from parent immediately. Keep dentry->d_inode intact so any
     * existing open handles remain valid. Persistent inode backing is freed
     * later from axiomfs_delete_inode() when the VFS inode is destroyed. */
    list_del(&dentry->d_child);
    
    return 0;
}

int axiomfs_link(struct dentry *old, struct inode *dir, struct dentry *new) {
    struct inode *inode = old->d_inode;
    (void)dir;
    
    if (!inode) {
        return -ENOENT;
    }
    
    inode->i_nlink++;
    mark_inode_dirty(inode);
    
    ihold(inode);
    d_instantiate(new, inode);
    
    return 0;
}

int axiomfs_rename(struct inode *old_dir, struct dentry *old_dentry,
                   struct inode *new_dir, struct dentry *new_dentry) {
    struct inode *old_inode = old_dentry->d_inode;
    struct inode *new_inode = new_dentry->d_inode;
    
    if (!old_inode) {
        return -ENOENT;
    }
    
    /* If target exists, remove it */
    if (new_inode) {
        if (S_ISDIR(new_inode->i_mode)) {
            int ret = axiomfs_rmdir(new_dir, new_dentry);
            if (ret) return ret;
        } else {
            int ret = axiomfs_unlink(new_dir, new_dentry);
            if (ret) return ret;
        }
    }
    
    /* Update directory links if moving directory */
    if (S_ISDIR(old_inode->i_mode)) {
        old_dir->i_nlink--;
        new_dir->i_nlink++;
        mark_inode_dirty(old_dir);
        mark_inode_dirty(new_dir);
    }
    
    /* Update dentry */
    strncpy(new_dentry->d_name, old_dentry->d_name, NAME_MAX);
    d_instantiate(new_dentry, old_inode);
    ihold(old_inode);
    
    d_delete(old_dentry);
    
    return 0;
}

struct dentry *axiomfs_lookup(struct inode *dir, struct dentry *dentry, 
                              unsigned int flags) {
    (void)flags;
    
    /* Check policy for read access to directory */
    if (policy_daemon_available()) {
        int result = policy_check_access(
            current ? current->cred->fsuid : 0,
            current ? current->cred->fsgid : 0,
            dir->i_ino,
            dentry->d_name,
            POLICY_OP_READ
        );
        
        if (result == POLICY_DENY) {
            return ERR_PTR(-EACCES);
        }
    }
    
    struct dentry *child;
    struct dentry *parent_dentry = dentry->d_parent;
    if (!parent_dentry) {
        return NULL;
    }

    /* If an existing child dentry matches, return it so VFS doesn't create
     * duplicate alias dentries for the same name. */
    list_for_each_entry(child, &parent_dentry->d_subdirs, d_child) {
        if (strcmp(child->d_name, dentry->d_name) == 0 && child->d_inode) {
            return dget(child);
        }
    }

    return NULL;
}