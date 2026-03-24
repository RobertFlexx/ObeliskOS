/*
 * Obelisk OS - Virtual Filesystem Header
 * From Axioms, Order.
 */

#ifndef _FS_VFS_H
#define _FS_VFS_H

#include <obelisk/types.h>
#include <obelisk/limits.h>
#include <uapi/syscall.h>

/* Forward declarations */
struct file;
struct inode;
struct dentry;
struct super_block;
struct file_system_type;
struct vfsmount;
struct iattr;
struct kstat;
struct kstatfs;
struct qstr;
struct vm_area;
struct dir_context;

/* File types */
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12
#define DT_WHT      14

/* Inode operations */
struct inode_operations {
    struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
    int (*create)(struct inode *, struct dentry *, mode_t, bool);
    int (*link)(struct dentry *, struct inode *, struct dentry *);
    int (*unlink)(struct inode *, struct dentry *);
    int (*symlink)(struct inode *, struct dentry *, const char *);
    int (*mkdir)(struct inode *, struct dentry *, mode_t);
    int (*rmdir)(struct inode *, struct dentry *);
    int (*mknod)(struct inode *, struct dentry *, mode_t, dev_t);
    int (*rename)(struct inode *, struct dentry *, struct inode *, struct dentry *);
    int (*readlink)(struct dentry *, char *, int);
    int (*permission)(struct inode *, int);
    int (*setattr)(struct dentry *, struct iattr *);
    int (*getattr)(const struct dentry *, struct kstat *);
};

/* File operations */
struct file_operations {
    loff_t (*llseek)(struct file *, loff_t, int);
    ssize_t (*read)(struct file *, char *, size_t, loff_t *);
    ssize_t (*write)(struct file *, const char *, size_t, loff_t *);
    int (*readdir)(struct file *, struct dir_context *);
    int (*ioctl)(struct file *, unsigned int, unsigned long);
    int (*mmap)(struct file *, struct vm_area *);
    int (*open)(struct inode *, struct file *);
    int (*release)(struct inode *, struct file *);
    int (*fsync)(struct file *, int);
};

/* Superblock operations */
struct super_operations {
    struct inode *(*alloc_inode)(struct super_block *);
    void (*destroy_inode)(struct inode *);
    void (*dirty_inode)(struct inode *);
    int (*write_inode)(struct inode *, bool);
    void (*drop_inode)(struct inode *);
    void (*delete_inode)(struct inode *);
    void (*put_super)(struct super_block *);
    int (*sync_fs)(struct super_block *, int);
    int (*statfs)(struct dentry *, struct kstatfs *);
    int (*remount_fs)(struct super_block *, int *, char *);
};

/* Directory entry operations */
struct dentry_operations {
    int (*d_revalidate)(struct dentry *, unsigned int);
    int (*d_hash)(const struct dentry *, struct qstr *);
    int (*d_compare)(const struct dentry *, const struct dentry *,
                     unsigned int, const char *, const struct qstr *);
    int (*d_delete)(const struct dentry *);
    void (*d_release)(struct dentry *);
};

/* Inode structure */
struct inode {
    ino_t i_ino;                    /* Inode number */
    mode_t i_mode;                  /* File mode */
    nlink_t i_nlink;                /* Link count */
    uid_t i_uid;                    /* Owner UID */
    gid_t i_gid;                    /* Owner GID */
    dev_t i_rdev;                   /* Device ID (if special file) */
    loff_t i_size;                  /* File size */
    time_t i_atime;                 /* Access time */
    time_t i_mtime;                 /* Modification time */
    time_t i_ctime;                 /* Change time */
    blksize_t i_blksize;            /* Block size */
    blkcnt_t i_blocks;              /* Block count */
    
    const struct inode_operations *i_op;    /* Inode operations */
    const struct file_operations *i_fop;    /* Default file operations */
    struct super_block *i_sb;       /* Superblock */
    
    void *i_private;                /* Filesystem private data */
    
    spinlock_t i_lock;              /* Inode lock */
    atomic_t i_count;               /* Reference count */
    uint32_t i_flags;               /* Inode flags */
    
    struct list_head i_list;        /* Inode list */
    struct list_head i_sb_list;     /* Superblock inode list */
    struct list_head i_dentry;      /* Dentries pointing to this inode */
};

/* Directory entry (dentry) */
struct dentry {
    char d_name[NAME_MAX + 1];      /* Entry name */
    struct inode *d_inode;          /* Associated inode */
    struct dentry *d_parent;        /* Parent directory */
    struct super_block *d_sb;       /* Superblock */
    
    const struct dentry_operations *d_op;   /* Dentry operations */
    
    struct list_head d_child;       /* Child of parent */
    struct list_head d_subdirs;     /* Subdirectories */
    struct list_head d_alias;       /* Alias in inode->i_dentry */
    
    atomic_t d_count;               /* Reference count */
    uint32_t d_flags;               /* Dentry flags */
    
    spinlock_t d_lock;
};

/* Open file structure */
struct file {
    struct dentry *f_dentry;        /* Associated dentry */
    struct vfsmount *f_vfsmnt;      /* Mount point */
    const struct file_operations *f_op; /* File operations */
    
    loff_t f_pos;                   /* Current position */
    uint32_t f_flags;               /* Open flags */
    mode_t f_mode;                  /* File mode */
    
    void *private_data;             /* Private data */
    
    atomic_t f_count;               /* Reference count */
    spinlock_t f_lock;
};

/* Superblock */
struct super_block {
    dev_t s_dev;                    /* Device */
    uint64_t s_blocksize;           /* Block size */
    uint64_t s_maxbytes;            /* Max file size */
    struct file_system_type *s_type;/* Filesystem type */
    const struct super_operations *s_op; /* Superblock operations */
    
    uint32_t s_flags;               /* Mount flags */
    uint32_t s_magic;               /* Filesystem magic number */
    struct dentry *s_root;          /* Root dentry */
    
    void *s_fs_info;                /* Filesystem private info */
    
    struct list_head s_inodes;      /* All inodes */
    struct list_head s_list;        /* Superblock list */
    
    spinlock_t s_lock;
    atomic_t s_count;
};

/* Filesystem type */
struct file_system_type {
    const char *name;               /* Filesystem name */
    int fs_flags;                   /* Filesystem flags */
    
    struct super_block *(*mount)(struct file_system_type *, int,
                                 const char *, void *);
    void (*kill_sb)(struct super_block *);
    
    struct list_head fs_list;       /* Registered filesystems */
};

/* Mount structure */
struct vfsmount {
    struct dentry *mnt_root;        /* Root of mounted tree */
    struct super_block *mnt_sb;     /* Superblock */
    struct dentry *mnt_mountpoint;  /* Where mounted */
    struct vfsmount *mnt_parent;    /* Parent mount */
    
    int mnt_flags;                  /* Mount flags */
    const char *mnt_devname;        /* Device name */
    
    struct list_head mnt_list;      /* Mount list */
    atomic_t mnt_count;             /* Reference count */
};

/* Stat structure for getattr */
struct kstat {
    dev_t dev;
    ino_t ino;
    mode_t mode;
    nlink_t nlink;
    uid_t uid;
    gid_t gid;
    dev_t rdev;
    loff_t size;
    time_t atime;
    time_t mtime;
    time_t ctime;
    blksize_t blksize;
    blkcnt_t blocks;
};

/* Statfs structure */
struct kstatfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
};

/* Directory iteration context */
struct dir_context {
    int (*actor)(struct dir_context *, const char *, int, loff_t, ino_t, unsigned);
    loff_t pos;
};

/* Inode attribute modification */
struct iattr {
    unsigned int ia_valid;
    mode_t ia_mode;
    uid_t ia_uid;
    gid_t ia_gid;
    loff_t ia_size;
    time_t ia_atime;
    time_t ia_mtime;
    time_t ia_ctime;
};

/* VFS initialization */
void vfs_init(void);

/* Filesystem registration */
int register_filesystem(struct file_system_type *fs);
int unregister_filesystem(struct file_system_type *fs);

/* Mount operations */
int vfs_mount(const char *source, const char *target,
              const char *fstype, unsigned long flags, void *data);
int vfs_umount(const char *target, int flags);

/* File operations */
struct file *vfs_open(const char *pathname, int flags, mode_t mode);
int vfs_close(struct file *file);
ssize_t vfs_read(struct file *file, void *buf, size_t count, loff_t *pos);
ssize_t vfs_write(struct file *file, const void *buf, size_t count, loff_t *pos);
loff_t vfs_lseek(struct file *file, loff_t offset, int whence);
int vfs_fsync(struct file *file);

/* Path operations */
struct dentry *vfs_lookup(const char *pathname);
int vfs_create(const char *pathname, mode_t mode);
int vfs_mkdir(const char *pathname, mode_t mode);
int vfs_rmdir(const char *pathname);
int vfs_unlink(const char *pathname);
int vfs_link(const char *oldpath, const char *newpath);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_rename(const char *oldpath, const char *newpath);

/* Stat operations */
int vfs_stat(const char *pathname, struct kstat *stat);
int vfs_fstat(struct file *file, struct kstat *stat);
int vfs_lstat(const char *pathname, struct kstat *stat);

/* Inode operations */
struct inode *inode_alloc(struct super_block *sb);
void inode_free(struct inode *inode);
struct inode *iget(struct super_block *sb, ino_t ino);
void iput(struct inode *inode);

/* Dentry operations */
struct dentry *dentry_alloc(struct dentry *parent, const char *name);
struct dentry *d_make_root(struct inode *root_inode);
void dentry_free(struct dentry *dentry);
struct dentry *dget(struct dentry *dentry);
void dput(struct dentry *dentry);
void d_add(struct dentry *dentry, struct inode *inode);
void d_delete(struct dentry *dentry);
void d_instantiate(struct dentry *dentry, struct inode *inode);

/* File helper */
struct file *file_alloc(void);
void file_free(struct file *file);
struct file *fget(int fd);
void fput(struct file *file);

#endif /* _FS_VFS_H */