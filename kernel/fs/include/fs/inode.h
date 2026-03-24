/*
 * Obelisk OS - Inode Header
 * From Axioms, Order.
 */

#ifndef _FS_INODE_H
#define _FS_INODE_H

#include <obelisk/types.h>
#include <fs/vfs.h>
#include <uapi/syscall.h>

#ifndef MAY_EXEC
#define MAY_EXEC    0x1
#define MAY_WRITE   0x2
#define MAY_READ    0x4
#endif

/* Inode flags */
#define I_DIRTY         BIT(0)      /* Inode is dirty */
#define I_LOCK          BIT(1)      /* Inode is locked */
#define I_NEW           BIT(2)      /* Inode is new */
#define I_FREEING       BIT(3)      /* Inode is being freed */
#define I_CLEAR         BIT(4)      /* Inode is being cleared */
#define I_SYNC          BIT(5)      /* Sync in progress */
#define I_REFERENCED    BIT(6)      /* Recently accessed */

/* Inode state bits */
#define I_DIRTY_SYNC        BIT(0)
#define I_DIRTY_DATASYNC    BIT(1)
#define I_DIRTY_PAGES       BIT(2)

/* File type macros */
#define S_ISDIR(m)      (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)      (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)      (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)      (((m) & S_IFMT) == S_IFREG)
#define S_ISFIFO(m)     (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)      (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m)     (((m) & S_IFMT) == S_IFSOCK)

/* Inode cache */
extern struct kmem_cache *inode_cache;

/* Inode hash table */
#define INODE_HASH_BITS     12
#define INODE_HASH_SIZE     (1 << INODE_HASH_BITS)

/* Inode cache operations */
void inode_cache_init(void);
struct inode *inode_cache_lookup(struct super_block *sb, ino_t ino);
void inode_cache_insert(struct inode *inode);
void inode_cache_remove(struct inode *inode);

/* Inode allocation */
struct inode *new_inode(struct super_block *sb);
void free_inode(struct inode *inode);

/* Inode reference counting */
struct inode *igrab(struct inode *inode);
void ihold(struct inode *inode);
void iput(struct inode *inode);

/* Inode locking */
void inode_lock(struct inode *inode);
void inode_unlock(struct inode *inode);
bool inode_trylock(struct inode *inode);

/* Inode operations */
void inode_init_once(struct inode *inode);
void inode_init_owner(struct inode *inode, const struct inode *dir, mode_t mode);
void mark_inode_dirty(struct inode *inode);
void clear_inode(struct inode *inode);

/* Inode lookup */
struct inode *iget_locked(struct super_block *sb, ino_t ino);
void unlock_new_inode(struct inode *inode);

/* Inode writeback */
int write_inode_now(struct inode *inode, int sync);
int sync_inode(struct inode *inode);

/* Generic inode operations */
int generic_permission(struct inode *inode, int mask);
int generic_setattr(struct dentry *dentry, struct iattr *attr);
int generic_getattr(const struct dentry *dentry, struct kstat *stat);

/* Simple filesystem helpers */
int simple_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);
int simple_link(struct dentry *old, struct inode *dir, struct dentry *new);
int simple_unlink(struct inode *dir, struct dentry *dentry);
int simple_rmdir(struct inode *dir, struct dentry *dentry);
int simple_rename(struct inode *old_dir, struct dentry *old_dentry,
                  struct inode *new_dir, struct dentry *new_dentry);

/* Timestamp helpers */
void inode_set_atime(struct inode *inode, time_t sec);
void inode_set_mtime(struct inode *inode, time_t sec);
void inode_set_ctime(struct inode *inode, time_t sec);
void inode_update_time(struct inode *inode, int flags);

#endif /* _FS_INODE_H */