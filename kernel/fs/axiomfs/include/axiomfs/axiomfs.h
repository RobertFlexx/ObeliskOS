/*
 * Obelisk OS - AxiomFS Header
 * From Axioms, Order.
 *
 * AxiomFS is a logic-driven filesystem with policy delegation to Prolog.
 */

#ifndef _AXIOMFS_AXIOMFS_H
#define _AXIOMFS_AXIOMFS_H

#include <obelisk/types.h>
#include <fs/vfs.h>

/* AxiomFS magic number */
#define AXIOMFS_MAGIC           0x41584653  /* "AXFS" */
#define AXIOMFS_VERSION         1

/* On-disk superblock */
struct axiomfs_super_block {
    uint32_t s_magic;               /* Magic number */
    uint32_t s_version;             /* Filesystem version */
    uint64_t s_block_count;         /* Total blocks */
    uint64_t s_inode_count;         /* Total inodes */
    uint32_t s_block_size;          /* Block size */
    uint32_t s_inodes_per_block;    /* Inodes per block */
    uint64_t s_inode_bitmap_block;  /* Inode bitmap start */
    uint64_t s_block_bitmap_block;  /* Block bitmap start */
    uint64_t s_inode_table_block;   /* Inode table start */
    uint64_t s_data_blocks_start;   /* Data blocks start */
    uint64_t s_root_inode;          /* Root directory inode */
    uint64_t s_free_blocks;         /* Free block count */
    uint64_t s_free_inodes;         /* Free inode count */
    uint8_t  s_uuid[16];            /* Filesystem UUID */
    uint64_t s_mount_time;          /* Last mount time */
    uint64_t s_write_time;          /* Last write time */
    uint32_t s_mount_count;         /* Mount count */
    uint32_t s_state;               /* Filesystem state */
    uint32_t s_flags;               /* Filesystem flags */
    uint8_t  s_reserved[920];       /* Reserved for future use */
} __packed;

/* Filesystem states */
#define AXIOMFS_STATE_CLEAN     0
#define AXIOMFS_STATE_DIRTY     1
#define AXIOMFS_STATE_ERROR     2

/* Filesystem flags */
#define AXIOMFS_FLAG_POLICY     BIT(0)  /* Policy daemon enabled */
#define AXIOMFS_FLAG_JOURNAL    BIT(1)  /* Journaling enabled */
#define AXIOMFS_FLAG_COMPRESS   BIT(2)  /* Compression enabled */

/* On-disk inode */
struct axiomfs_inode {
    uint32_t i_mode;                /* File mode */
    uint32_t i_uid;                 /* Owner UID */
    uint32_t i_gid;                 /* Owner GID */
    uint32_t i_flags;               /* Inode flags */
    uint64_t i_size;                /* File size */
    uint64_t i_atime;               /* Access time */
    uint64_t i_mtime;               /* Modification time */
    uint64_t i_ctime;               /* Change time */
    uint32_t i_links_count;         /* Hard link count */
    uint32_t i_blocks;              /* Block count */
    uint64_t i_policy_hash;         /* Policy rule hash */
    uint64_t i_direct[12];          /* Direct block pointers */
    uint64_t i_indirect;            /* Indirect block pointer */
    uint64_t i_double_indirect;     /* Double indirect */
    uint64_t i_triple_indirect;     /* Triple indirect */
    uint8_t  i_reserved[64];        /* Reserved */
} __packed;

/* On-disk directory entry */
struct axiomfs_dirent {
    uint64_t d_inode;               /* Inode number */
    uint16_t d_rec_len;             /* Record length */
    uint8_t  d_name_len;            /* Name length */
    uint8_t  d_file_type;           /* File type */
    char     d_name[252];           /* Filename */
} __packed;

/* In-memory superblock info */
struct axiomfs_sb_info {
    struct axiomfs_super_block *s_sb;   /* On-disk superblock */
    uint64_t *s_inode_bitmap;           /* Inode bitmap */
    uint64_t *s_block_bitmap;           /* Block bitmap */
    uint64_t s_inode_bitmap_blocks;     /* Bitmap size in blocks */
    uint64_t s_block_bitmap_blocks;     /* Bitmap size in blocks */
    spinlock_t s_lock;                  /* Superblock lock */
    bool s_policy_enabled;              /* Policy daemon enabled */
    void *s_policy_cache;               /* Policy decision cache */
};

/* In-memory inode info */
struct axiomfs_inode_info {
    struct axiomfs_inode *ai_inode;     /* On-disk inode */
    uint64_t ai_block;                  /* Inode block number */
    bool ai_policy_cached;              /* Policy decisions cached */
    uint64_t ai_policy_hash;            /* Current policy hash */
};

/* Block device operations (stub for now) */
struct block_device;

int axiomfs_read_block(struct super_block *sb, uint64_t block, void *buf);
int axiomfs_write_block(struct super_block *sb, uint64_t block, const void *buf);

/* Superblock operations */
void axiomfs_init(void);
struct super_block *axiomfs_mount(struct file_system_type *fs_type,
                                  int flags, const char *dev, void *data);
void axiomfs_kill_sb(struct super_block *sb);

/* Inode operations */
struct inode *axiomfs_iget(struct super_block *sb, ino_t ino);
int axiomfs_write_inode(struct inode *inode, bool sync);
void axiomfs_delete_inode(struct inode *inode);
void axiomfs_evict_inode(struct inode *inode);

/* Directory operations */
int axiomfs_create(struct inode *dir, struct dentry *dentry, mode_t mode, bool excl);
struct dentry *axiomfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);
int axiomfs_mkdir(struct inode *dir, struct dentry *dentry, mode_t mode);
int axiomfs_rmdir(struct inode *dir, struct dentry *dentry);
int axiomfs_unlink(struct inode *dir, struct dentry *dentry);
int axiomfs_link(struct dentry *old, struct inode *dir, struct dentry *new);
int axiomfs_rename(struct inode *old_dir, struct dentry *old_dentry,
                   struct inode *new_dir, struct dentry *new_dentry);

/* File operations */
ssize_t axiomfs_read(struct file *file, char *buf, size_t count, loff_t *pos);
ssize_t axiomfs_write(struct file *file, const char *buf, size_t count, loff_t *pos);
int axiomfs_readdir(struct file *file, struct dir_context *ctx);

/* Block allocation */
uint64_t axiomfs_alloc_block(struct super_block *sb);
void axiomfs_free_block(struct super_block *sb, uint64_t block);
uint64_t axiomfs_alloc_inode_num(struct super_block *sb);
void axiomfs_free_inode_num(struct super_block *sb, uint64_t ino);

/* Policy interface */
int axiomfs_policy_check_access(struct inode *inode, int mask);
int axiomfs_policy_get_allocation(struct inode *inode, uint64_t *block);
int axiomfs_policy_inherit_permissions(struct inode *parent, struct inode *child);

#endif /* _AXIOMFS_AXIOMFS_H */