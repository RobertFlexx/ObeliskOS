/*
 * Obelisk OS - File Header
 * From Axioms, Order.
 */

#ifndef _FS_FILE_H
#define _FS_FILE_H

#include <obelisk/types.h>
#include <fs/vfs.h>

/* File flags (for f_flags) */
#define FMODE_READ          0x1
#define FMODE_WRITE         0x2
#define FMODE_EXEC          0x4
#define FMODE_LSEEK         0x8
#define FMODE_PREAD         0x10
#define FMODE_PWRITE        0x20
#define FMODE_ATOMIC_POS    0x40
#define FMODE_NOCMTIME      0x80

/* File descriptor flags */
#define FD_CLOEXEC          0x1

/* File table */
extern struct kmem_cache *file_cache;

/* File allocation/free */
struct file *alloc_file(struct dentry *dentry, mode_t mode,
                        const struct file_operations *fops);
void free_file(struct file *file);

/* File reference counting */
struct file *get_file(struct file *file);
void put_file(struct file *file);
void fput(struct file *file);
struct file *fget(int fd);

/* File descriptor operations */
int get_unused_fd_flags(unsigned int flags);
void put_unused_fd(int fd);
void fd_install(int fd, struct file *file);
int close_fd(int fd);

/* File position */
loff_t file_pos_read(struct file *file);
void file_pos_write(struct file *file, loff_t pos);

/* Generic file operations */
loff_t generic_file_llseek(struct file *file, loff_t offset, int whence);
ssize_t generic_file_read(struct file *file, char *buf, size_t count, loff_t *pos);
ssize_t generic_file_write(struct file *file, const char *buf, size_t count, loff_t *pos);
int generic_file_open(struct inode *inode, struct file *file);
int generic_file_release(struct inode *inode, struct file *file);

/* Directory operations */
int dcache_readdir(struct file *file, struct dir_context *ctx);

/* Simple file operations */
ssize_t simple_read_from_buffer(void *to, size_t count, loff_t *ppos,
                                const void *from, size_t available);
ssize_t simple_write_to_buffer(void *to, size_t available, loff_t *ppos,
                               const void *from, size_t count);

/* File mode helpers */
static inline bool file_readable(struct file *file) {
    return (file->f_mode & FMODE_READ) != 0;
}

static inline bool file_writable(struct file *file) {
    return (file->f_mode & FMODE_WRITE) != 0;
}

/* Read/write helpers */
ssize_t vfs_read(struct file *file, void *buf, size_t count, loff_t *pos);
ssize_t vfs_write(struct file *file, const void *buf, size_t count, loff_t *pos);
ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos);
ssize_t kernel_write(struct file *file, const void *buf, size_t count, loff_t *pos);

#endif /* _FS_FILE_H */