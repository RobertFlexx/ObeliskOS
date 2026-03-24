/*
 * Obelisk OS - AxiomFS Superblock Operations
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <fs/file.h>
#include <axiomfs/axiomfs.h>
#include <axiomfs/policy.h>
#include <mm/kmalloc.h>

/* Filesystem type */
static struct file_system_type axiomfs_fs_type = {
    .name = "axiomfs",
    .fs_flags = 0,
    .mount = axiomfs_mount,
    .kill_sb = axiomfs_kill_sb,
};

/* Superblock operations */
static const struct super_operations axiomfs_super_ops = {
    .alloc_inode = NULL,
    .destroy_inode = NULL,
    .write_inode = (void *)axiomfs_write_inode,
    .drop_inode = NULL,
    .delete_inode = NULL,
    .put_super = NULL,
    .sync_fs = NULL,
    .statfs = NULL,
};

/* Inode operations for directories */
const struct inode_operations axiomfs_dir_inode_ops = {
    .lookup = axiomfs_lookup,
    .create = axiomfs_create,
    .mkdir = axiomfs_mkdir,
    .rmdir = axiomfs_rmdir,
    .unlink = axiomfs_unlink,
    .link = axiomfs_link,
    .rename = axiomfs_rename,
};

/* Inode operations for files */
const struct inode_operations axiomfs_file_inode_ops = {
    .lookup = NULL,
    .getattr = generic_getattr,
};

/* File operations for directories */
const struct file_operations axiomfs_dir_ops = {
    .readdir = axiomfs_readdir,
    .llseek = generic_file_llseek,
};

/* File operations for regular files */
const struct file_operations axiomfs_file_ops = {
    .read = axiomfs_read,
    .write = axiomfs_write,
    .llseek = generic_file_llseek,
    .open = generic_file_open,
    .release = generic_file_release,
};

/* ==========================================================================
 * Mount/Unmount
 * ========================================================================== */

struct super_block *axiomfs_mount(struct file_system_type *fs_type,
                                  int flags, const char *dev, void *data) {
    struct super_block *sb;
    struct axiomfs_sb_info *sbi;
    struct axiomfs_super_block *asb;
    struct inode *root_inode;
    
    (void)fs_type;
    (void)flags;
    (void)data;
    
    printk(KERN_INFO "AxiomFS: Mounting %s\n", dev ? dev : "nodev");
    
    /* Allocate superblock */
    sb = kzalloc(sizeof(struct super_block));
    if (!sb) {
        return ERR_PTR(-ENOMEM);
    }
    
    /* Allocate AxiomFS superblock info */
    sbi = kzalloc(sizeof(struct axiomfs_sb_info));
    if (!sbi) {
        kfree(sb);
        return ERR_PTR(-ENOMEM);
    }
    
    /* Allocate on-disk superblock buffer */
    asb = kzalloc(sizeof(struct axiomfs_super_block));
    if (!asb) {
        kfree(sbi);
        kfree(sb);
        return ERR_PTR(-ENOMEM);
    }
    
    /* Initialize on-disk superblock with defaults (for in-memory FS) */
    asb->s_magic = AXIOMFS_MAGIC;
    asb->s_version = AXIOMFS_VERSION;
    asb->s_block_size = 4096;
    asb->s_block_count = 1024 * 1024;   /* 4GB */
    asb->s_inode_count = 65536;
    asb->s_root_inode = 1;
    asb->s_free_blocks = asb->s_block_count - 100;
    asb->s_free_inodes = asb->s_inode_count - 1;
    asb->s_state = AXIOMFS_STATE_CLEAN;
    
    sbi->s_sb = asb;
    sbi->s_lock = (spinlock_t)SPINLOCK_INIT;
    sbi->s_policy_enabled = true;
    sbi->s_policy_cache = NULL;  /* Will be initialized by policy_init */
    
    /* Initialize VFS superblock */
    sb->s_blocksize = asb->s_block_size;
    sb->s_maxbytes = INT64_MAX;
    sb->s_type = &axiomfs_fs_type;
    sb->s_op = &axiomfs_super_ops;
    sb->s_magic = AXIOMFS_MAGIC;
    sb->s_fs_info = sbi;
    sb->s_flags = flags;
    sb->s_count.counter = 1;
    sb->s_lock = (spinlock_t)SPINLOCK_INIT;
    INIT_LIST_HEAD(&sb->s_inodes);
    
    /* Create root inode */
    root_inode = new_inode(sb);
    if (!root_inode) {
        kfree(asb);
        kfree(sbi);
        kfree(sb);
        return ERR_PTR(-ENOMEM);
    }
    
    root_inode->i_ino = asb->s_root_inode;
    root_inode->i_mode = S_IFDIR | 0755;
    root_inode->i_uid = 0;
    root_inode->i_gid = 0;
    root_inode->i_size = 0;
    root_inode->i_nlink = 2;
    root_inode->i_op = &axiomfs_dir_inode_ops;
    root_inode->i_fop = &axiomfs_dir_ops;
    
    /* Create root dentry */
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        iput(root_inode);
        kfree(asb);
        kfree(sbi);
        kfree(sb);
        return ERR_PTR(-ENOMEM);
    }
    
    printk(KERN_INFO "AxiomFS: Mounted successfully\n");
    printk(KERN_INFO "AxiomFS: Block size: %u, Blocks: %lu, Inodes: %lu\n",
           asb->s_block_size, asb->s_block_count, asb->s_inode_count);
    
    return sb;
}

void axiomfs_kill_sb(struct super_block *sb) {
    struct axiomfs_sb_info *sbi = sb->s_fs_info;
    
    printk(KERN_INFO "AxiomFS: Unmounting\n");
    
    if (sbi) {
        if (sbi->s_sb) {
            kfree(sbi->s_sb);
        }
        kfree(sbi);
    }
    
    if (sb->s_root) {
        dput(sb->s_root);
    }
    
    kfree(sb);
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void axiomfs_init(void) {
    printk(KERN_INFO "AxiomFS: Initializing...\n");
    
    /* Register filesystem */
    register_filesystem(&axiomfs_fs_type);
    
    /* Initialize policy subsystem */
    policy_init();
    
    printk(KERN_INFO "AxiomFS: Initialized\n");
}