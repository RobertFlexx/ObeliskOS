/*
 * Obelisk OS - File Operations
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <fs/vfs.h>
#include <fs/file.h>
#include <mm/kmalloc.h>
#include <proc/process.h>

/* File cache */
extern struct kmem_cache *file_cache;

/* ==========================================================================
 * File Allocation
 * ========================================================================== */

struct file *alloc_file(struct dentry *dentry, mode_t mode,
                        const struct file_operations *fops) {
    struct file *file;
    
    file = kmem_cache_zalloc(file_cache);
    if (!file) {
        return NULL;
    }
    
    file->f_dentry = dget(dentry);
    file->f_op = fops;
    file->f_mode = mode;
    file->f_flags = 0;
    file->f_pos = 0;
    file->f_count.counter = 1;
    file->f_lock = (spinlock_t)SPINLOCK_INIT;
    
    return file;
}

void free_file(struct file *file) {
    if (!file) return;
    
    if (file->f_dentry) {
        dput(file->f_dentry);
    }
    
    kmem_cache_free(file_cache, file);
}

/* ==========================================================================
 * Reference Counting
 * ========================================================================== */

struct file *get_file(struct file *file) {
    if (file) {
        file->f_count.counter++;
    }
    return file;
}

void put_file(struct file *file) {
    if (!file) return;
    
    if (--file->f_count.counter == 0) {
        free_file(file);
    }
}

void fput(struct file *file) {
    put_file(file);
}

struct file *fget(int fd) {
    struct process *proc = current;
    
    if (!proc || !proc->files) {
        return NULL;
    }
    
    struct file *file = fd_get(proc->files, fd);
    if (file) {
        get_file(file);
    }
    
    return file;
}

/* ==========================================================================
 * File Descriptor Operations
 * ========================================================================== */

int get_unused_fd_flags(unsigned int flags) {
    struct process *proc = current;
    
    if (!proc || !proc->files) {
        return -EBADF;
    }
    
    for (size_t i = 3; i < proc->files->max_fds; i++) {
        if (proc->files->fds[i].file == NULL) {
            proc->files->fds[i].flags = flags;
            return i;
        }
    }
    
    return -EMFILE;
}

void put_unused_fd(int fd) {
    struct process *proc = current;
    
    if (proc && proc->files && fd >= 3 && fd < (int)proc->files->max_fds) {
        proc->files->fds[fd].file = NULL;
        proc->files->fds[fd].flags = 0;
    }
}

void fd_install(int fd, struct file *file) {
    struct process *proc = current;
    
    if (proc && proc->files && fd >= 0 && fd < (int)proc->files->max_fds) {
        proc->files->fds[fd].file = file;
    }
}

int close_fd(int fd) {
    struct process *proc = current;
    
    if (!proc || !proc->files) {
        return -EBADF;
    }
    
    if (fd < 0 || fd >= (int)proc->files->max_fds) {
        return -EBADF;
    }
    if (fd <= 2) {
        return 0;
    }
    
    struct file *file = proc->files->fds[fd].file;
    if (!file) {
        return -EBADF;
    }
    
    proc->files->fds[fd].file = NULL;
    proc->files->fds[fd].flags = 0;
    
    return vfs_close(file);
}

/* ==========================================================================
 * Generic File Operations
 * ========================================================================== */

ssize_t generic_file_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    /* To be implemented by specific filesystems */
    (void)file; (void)buf; (void)count; (void)pos;
    return -EINVAL;
}

ssize_t generic_file_write(struct file *file, const char *buf, size_t count, loff_t *pos) {
    /* To be implemented by specific filesystems */
    (void)file; (void)buf; (void)count; (void)pos;
    return -EINVAL;
}

int generic_file_open(struct inode *inode, struct file *file) {
    (void)inode; (void)file;
    return 0;
}

int generic_file_release(struct inode *inode, struct file *file) {
    (void)inode; (void)file;
    return 0;
}

/* ==========================================================================
 * Buffer Helpers
 * ========================================================================== */

ssize_t simple_read_from_buffer(void *to, size_t count, loff_t *ppos,
                                const void *from, size_t available) {
    loff_t pos = *ppos;
    
    if (pos < 0) {
        return -EINVAL;
    }
    
    if (pos >= (loff_t)available) {
        return 0;
    }
    
    if (count > available - pos) {
        count = available - pos;
    }
    
    memcpy(to, (const char *)from + pos, count);
    *ppos = pos + count;
    
    return count;
}

ssize_t simple_write_to_buffer(void *to, size_t available, loff_t *ppos,
                               const void *from, size_t count) {
    loff_t pos = *ppos;
    
    if (pos < 0) {
        return -EINVAL;
    }
    
    if (pos >= (loff_t)available) {
        return -ENOSPC;
    }
    
    if (count > available - pos) {
        count = available - pos;
    }
    
    memcpy((char *)to + pos, from, count);
    *ppos = pos + count;
    
    return count;
}