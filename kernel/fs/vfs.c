/*
 * Obelisk OS - Virtual Filesystem
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <fs/file.h>
#include <mm/kmalloc.h>

/* Registered filesystems */
static LIST_HEAD(file_systems);

/* Mount table */
#define MAX_MOUNTS  256
static struct vfsmount *mount_table[MAX_MOUNTS];
static int num_mounts = 0;

/* Root filesystem */
static struct vfsmount *root_mount = NULL;
static struct dentry *root_dentry = NULL;

static bool vfs_is_mountpoint(struct dentry *dentry) {
    if (!dentry) {
        return false;
    }
    /* Root itself. */
    if (root_mount && root_mount->mnt_root == dentry) {
        return true;
    }
    /* Any mountpoint we recorded during vfs_mount(). */
    for (int i = 0; i < num_mounts; i++) {
        if (mount_table[i] && mount_table[i]->mnt_mountpoint == dentry) {
            return true;
        }
    }
    return false;
}

/* Caches */
struct kmem_cache *inode_cache;
struct kmem_cache *dentry_cache;
struct kmem_cache *file_cache;

static int vfs_check_permission(struct inode *inode, int mask) {
    if (!inode) {
        return -ENOENT;
    }
    if (mask == 0) {
        return 0;
    }
    if (inode->i_op && inode->i_op->permission) {
        return inode->i_op->permission(inode, mask);
    }
    return generic_permission(inode, mask);
}

static bool vfs_trace_path(const char *pathname) {
    (void)pathname;
    return false;
}

/* ==========================================================================
 * Filesystem Registration
 * ========================================================================== */

int register_filesystem(struct file_system_type *fs) {
    struct file_system_type *tmp;
    
    if (!fs || !fs->name) {
        return -EINVAL;
    }
    
    /* Check for duplicates */
    list_for_each_entry(tmp, &file_systems, fs_list) {
        if (strcmp(tmp->name, fs->name) == 0) {
            return -EEXIST;
        }
    }
    
    list_add(&fs->fs_list, &file_systems);
    
    printk(KERN_INFO "VFS: Registered filesystem '%s'\n", fs->name);
    
    return 0;
}

int unregister_filesystem(struct file_system_type *fs) {
    if (!fs) {
        return -EINVAL;
    }
    
    list_del(&fs->fs_list);
    
    printk(KERN_INFO "VFS: Unregistered filesystem '%s'\n", fs->name);
    
    return 0;
}

static struct file_system_type *find_filesystem(const char *name) {
    struct file_system_type *fs;
    
    list_for_each_entry(fs, &file_systems, fs_list) {
        if (strcmp(fs->name, name) == 0) {
            return fs;
        }
    }
    
    return NULL;
}

/* ==========================================================================
 * Mount Operations
 * ========================================================================== */

int vfs_mount(const char *source, const char *target,
              const char *fstype, unsigned long flags, void *data) {
    struct file_system_type *fs;
    struct super_block *sb;
    struct vfsmount *mnt;
    struct dentry *mountpoint;
    
    printk(KERN_DEBUG "VFS: Mounting %s at %s (type=%s)\n",
           source ? source : "none", target, fstype);
    
    /* Find filesystem type */
    fs = find_filesystem(fstype);
    if (!fs) {
        printk(KERN_ERR "VFS: Unknown filesystem type '%s'\n", fstype);
        return -ENODEV;
    }
    
    /* Get mount point (for non-root mounts) */
    if (root_dentry && target) {
        mountpoint = vfs_lookup(target);
        if (!mountpoint) {
            return -ENOENT;
        }
    } else {
        mountpoint = NULL;
    }
    
    /* Mount the filesystem */
    sb = fs->mount(fs, flags, source, data);
    if (IS_ERR(sb)) {
        return PTR_ERR(sb);
    }
    
    /* Create vfsmount */
    mnt = kzalloc(sizeof(struct vfsmount));
    if (!mnt) {
        fs->kill_sb(sb);
        return -ENOMEM;
    }
    
    mnt->mnt_sb = sb;
    mnt->mnt_root = sb->s_root;
    mnt->mnt_mountpoint = mountpoint;
    mnt->mnt_devname = source ? strdup(source) : NULL;
    mnt->mnt_flags = flags;
    mnt->mnt_count.counter = 1;
    
    /* Add to mount table */
    if (num_mounts < MAX_MOUNTS) {
        mount_table[num_mounts++] = mnt;
    }
    
    /* Set as root if first mount */
    if (!root_mount) {
        root_mount = mnt;
        root_dentry = sb->s_root;
        printk(KERN_INFO "VFS: Mounted root filesystem\n");
    }
    
    return 0;
}

int vfs_umount(const char *target, int flags) {
    /* TODO: Implement unmount */
    (void)target;
    (void)flags;
    return -ENOSYS;
}

/* ==========================================================================
 * Path Lookup
 * ========================================================================== */

static struct dentry *lookup_one(struct dentry *parent, const char *name, int len) {
    struct dentry *dentry;
    struct inode *dir;
    
    if (!parent || !parent->d_inode) {
        return NULL;
    }
    
    dir = parent->d_inode;
    
    /* Check for . and .. */
    if (len == 1 && name[0] == '.') {
        return dget(parent);
    }
    
    if (len == 2 && name[0] == '.' && name[1] == '.') {
        return dget(parent->d_parent ? parent->d_parent : parent);
    }
    
    /* Search in dcache first */
    struct list_head *pos;
    list_for_each(pos, &parent->d_subdirs) {
        dentry = list_entry(pos, struct dentry, d_child);
        if (strlen(dentry->d_name) == (size_t)len &&
            strncmp(dentry->d_name, name, len) == 0 &&
            dentry->d_inode) {
            return dget(dentry);
        }
    }
    
    /* Not in cache, do actual lookup */
    if (!dir->i_op || !dir->i_op->lookup) {
        return NULL;
    }
    
    dentry = dentry_alloc(parent, "");
    if (!dentry) {
        return NULL;
    }
    
    strncpy(dentry->d_name, name, len);
    dentry->d_name[len] = '\0';
    
    struct dentry *result = dir->i_op->lookup(dir, dentry, 0);
    if (IS_ERR(result)) {
        dput(dentry);
        return NULL;
    }
    
    if (result) {
        dput(dentry);
        dentry = result;
    }
    
    /* Treat negative dentries as not found for now to keep VFS behavior
     * deterministic during bring-up. */
    if (!dentry->d_inode) {
        dput(dentry);
        return NULL;
    }

    return dentry;
}

struct dentry *vfs_lookup(const char *pathname) {
    struct dentry *dentry;
    const char *p;
    int len;
    
    if (!pathname || !*pathname) {
        return NULL;
    }
    
    bool trace = vfs_trace_path(pathname);
    if (trace) {
        printk(KERN_INFO "VFS: lookup begin path=%s\n", pathname);
    }

    /* Start from root or current directory */
    if (pathname[0] == '/') {
        dentry = dget(root_dentry);
        pathname++;
    } else {
        /* TODO: Get current directory */
        dentry = dget(root_dentry);
    }
    
    if (!dentry) {
        return NULL;
    }
    
    /* Walk the path */
    while (*pathname) {
        /* Skip slashes */
        while (*pathname == '/') {
            pathname++;
        }
        
        if (!*pathname) {
            break;
        }
        
        /* Find component length */
        p = pathname;
        while (*p && *p != '/') {
            p++;
        }
        len = p - pathname;
        
        /* Enforce search permission on each traversed directory. */
        if (!dentry->d_inode || !S_ISDIR(dentry->d_inode->i_mode)) {
            dput(dentry);
            return NULL;
        }
        if (vfs_check_permission(dentry->d_inode, MAY_EXEC) < 0) {
            dput(dentry);
            return NULL;
        }

        /* Lookup component */
        if (trace) {
            char comp[64];
            int n = MIN(len, (int)sizeof(comp) - 1);
            memcpy(comp, pathname, (size_t)n);
            comp[n] = '\0';
            printk(KERN_INFO "VFS: lookup component='%s' on parent='%s'\n",
                   comp, dentry ? dentry->d_name : "(null)");
        }
        struct dentry *next = lookup_one(dentry, pathname, len);
        dput(dentry);
        
        if (!next) {
            if (trace) {
                printk(KERN_ERR "VFS: lookup failed component len=%d path=%s\n", len, pathname);
            }
            return NULL;
        }
        
        if (!next->d_inode) {
            dput(next);
            return NULL;
        }

        dentry = next;
        pathname = p;
    }
    if (!dentry->d_inode) {
        dput(dentry);
        return NULL;
    }

    if (trace) {
        printk(KERN_INFO "VFS: lookup success path resolved to '%s' inode=%p\n",
               dentry ? dentry->d_name : "(null)",
               dentry ? dentry->d_inode : NULL);
    }
    return dentry;
}

/* ==========================================================================
 * File Operations
 * ========================================================================== */

struct file *vfs_open(const char *pathname, int flags, mode_t mode) {
    struct dentry *dentry;
    struct inode *inode;
    struct file *file;
    int ret;
    
    /* Lookup path */
    dentry = vfs_lookup(pathname);
    
    if (!dentry) {
        /* File doesn't exist */
        if (!(flags & O_CREAT)) {
            return ERR_PTR(-ENOENT);
        }
        
        /* Create new file */
        ret = vfs_create(pathname, mode);
        if (ret < 0) {
            return ERR_PTR(ret);
        }
        
        dentry = vfs_lookup(pathname);
        if (!dentry) {
            return ERR_PTR(-ENOENT);
        }
    } else if (flags & O_EXCL) {
        dput(dentry);
        return ERR_PTR(-EEXIST);
    }
    
    inode = dentry->d_inode;
    if (!inode) {
        dput(dentry);
        return ERR_PTR(-ENOENT);
    }

    /* Enforce O_DIRECTORY semantics. */
    if ((flags & O_DIRECTORY) && !S_ISDIR(inode->i_mode)) {
        dput(dentry);
        return ERR_PTR(-ENOTDIR);
    }
    
    /* Check permissions */
    {
        int acc_mask = 0;
        int accmode = flags & O_ACCMODE;
        if (accmode == O_RDONLY) {
            acc_mask |= MAY_READ;
        } else if (accmode == O_WRONLY) {
            acc_mask |= MAY_WRITE;
        } else {
            acc_mask |= MAY_READ | MAY_WRITE;
        }
        if (flags & O_TRUNC) {
            acc_mask |= MAY_WRITE;
        }
        ret = vfs_check_permission(inode, acc_mask);
        if (ret < 0) {
            dput(dentry);
            return ERR_PTR(ret);
        }
    }
    
    /* Allocate file structure */
    file = kmem_cache_zalloc(file_cache);
    if (!file) {
        dput(dentry);
        return ERR_PTR(-ENOMEM);
    }
    
    file->f_dentry = dentry;
    file->f_op = inode->i_fop;
    file->f_flags = flags;
    file->f_pos = 0;
    file->f_count.counter = 1;
    file->f_lock = (spinlock_t)SPINLOCK_INIT;
    
    /* Set mode */
    file->f_mode = 0;
    if ((flags & O_ACCMODE) == O_RDONLY || (flags & O_ACCMODE) == O_RDWR) {
        file->f_mode |= FMODE_READ;
    }
    if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
        file->f_mode |= FMODE_WRITE;
    }
    
    /* Truncate if requested */
    if (flags & O_TRUNC) {
        /* TODO: Truncate file */
    }
    
    /* Seek to end if append mode */
    if (flags & O_APPEND) {
        file->f_pos = inode->i_size;
    }
    
    /* Call open handler */
    if (file->f_op && file->f_op->open) {
        ret = file->f_op->open(inode, file);
        if (ret < 0) {
            dput(dentry);
            kmem_cache_free(file_cache, file);
            return ERR_PTR(ret);
        }
    }
    
    return file;
}

int vfs_close(struct file *file) {
    if (!file) {
        return -EBADF;
    }
    
    /* Call release handler */
    if (file->f_op && file->f_op->release) {
        file->f_op->release(file->f_dentry->d_inode, file);
    }
    
    /* Release dentry */
    if (file->f_dentry) {
        dput(file->f_dentry);
    }
    
    kmem_cache_free(file_cache, file);
    
    return 0;
}

ssize_t vfs_read(struct file *file, void *buf, size_t count, loff_t *pos) {
    ssize_t ret;
    
    if (!file) {
        return -EBADF;
    }
    
    if (!(file->f_mode & FMODE_READ)) {
        return -EBADF;
    }
    
    if (!file->f_op || !file->f_op->read) {
        return -EINVAL;
    }
    
    loff_t ppos = pos ? *pos : file->f_pos;
    
    ret = file->f_op->read(file, buf, count, &ppos);
    
    if (ret > 0) {
        if (pos) {
            *pos = ppos;
        } else {
            file->f_pos = ppos;
        }
    }
    
    return ret;
}

ssize_t vfs_write(struct file *file, const void *buf, size_t count, loff_t *pos) {
    ssize_t ret;
    
    if (!file) {
        return -EBADF;
    }
    
    if (!(file->f_mode & FMODE_WRITE)) {
        return -EBADF;
    }
    
    if (!file->f_op || !file->f_op->write) {
        return -EINVAL;
    }
    
    loff_t ppos = pos ? *pos : file->f_pos;
    
    ret = file->f_op->write(file, buf, count, &ppos);
    
    if (ret > 0) {
        if (pos) {
            *pos = ppos;
        } else {
            file->f_pos = ppos;
        }
    }
    
    return ret;
}

loff_t vfs_lseek(struct file *file, loff_t offset, int whence) {
    loff_t ret;
    
    if (!file) {
        return -EBADF;
    }
    
    if (file->f_op && file->f_op->llseek) {
        ret = file->f_op->llseek(file, offset, whence);
    } else {
        ret = generic_file_llseek(file, offset, whence);
    }
    
    return ret;
}

int vfs_fsync(struct file *file) {
    if (!file) {
        return -EBADF;
    }
    
    if (file->f_op && file->f_op->fsync) {
        return file->f_op->fsync(file, 0);
    }
    
    return 0;
}

/* ==========================================================================
 * Directory Operations
 * ========================================================================== */

int vfs_create(const char *pathname, mode_t mode) {
    struct dentry *parent;
    struct dentry *dentry;
    struct inode *dir;
    char *name;
    char *parent_path;
    int ret;

    /* Avoid duplicate dentries for same path. */
    struct dentry *existing = vfs_lookup(pathname);
    if (existing) {
        dput(existing);
        return -EEXIST;
    }
    
    /* Split path into parent and name */
    name = strrchr(pathname, '/');
    if (name) {
        int parent_len = name - pathname;
        if (parent_len == 0) parent_len = 1;  /* Root */
        parent_path = kmalloc(parent_len + 1);
        if (!parent_path) {
            return -ENOMEM;
        }
        strncpy(parent_path, pathname, parent_len);
        parent_path[parent_len] = '\0';
        name++;
        
        parent = vfs_lookup(parent_path);
        kfree(parent_path);
    } else {
        parent = dget(root_dentry);
        name = (char *)pathname;
    }
    
    if (!parent) {
        return -ENOENT;
    }
    
    dir = parent->d_inode;
    if (!dir || !S_ISDIR(dir->i_mode)) {
        dput(parent);
        return -ENOTDIR;
    }
    ret = vfs_check_permission(dir, MAY_WRITE | MAY_EXEC);
    if (ret < 0) {
        dput(parent);
        return ret;
    }
    
    if (!dir->i_op || !dir->i_op->create) {
        dput(parent);
        return -EROFS;
    }
    
    /* Create dentry for new file */
    dentry = dentry_alloc(parent, name);
    if (!dentry) {
        dput(parent);
        return -ENOMEM;
    }
    
    ret = dir->i_op->create(dir, dentry, mode, false);
    
    if (ret < 0) {
        dput(dentry);
    }
    
    dput(parent);
    
    return ret;
}

int vfs_mkdir(const char *pathname, mode_t mode) {
    struct dentry *parent;
    struct dentry *dentry;
    struct inode *dir;
    char *name;
    char *parent_path;
    int ret;

    /* Avoid duplicate dentries for same path. */
    struct dentry *existing = vfs_lookup(pathname);
    if (existing) {
        dput(existing);
        return -EEXIST;
    }
    
    /* Split path */
    name = strrchr(pathname, '/');
    if (name) {
        int parent_len = name - pathname;
        if (parent_len == 0) parent_len = 1;  /* Root */
        parent_path = kmalloc(parent_len + 1);
        if (!parent_path) {
            return -ENOMEM;
        }
        strncpy(parent_path, pathname, parent_len);
        parent_path[parent_len] = '\0';
        name++;
        
        parent = vfs_lookup(parent_path);
        kfree(parent_path);
    } else {
        parent = dget(root_dentry);
        name = (char *)pathname;
    }
    
    if (!parent) {
        return -ENOENT;
    }
    
    dir = parent->d_inode;
    if (!dir || !S_ISDIR(dir->i_mode)) {
        dput(parent);
        return -ENOTDIR;
    }
    ret = vfs_check_permission(dir, MAY_WRITE | MAY_EXEC);
    if (ret < 0) {
        dput(parent);
        return ret;
    }
    
    if (!dir->i_op || !dir->i_op->mkdir) {
        dput(parent);
        return -EROFS;
    }
    
    dentry = dentry_alloc(parent, name);
    if (!dentry) {
        dput(parent);
        return -ENOMEM;
    }
    
    ret = dir->i_op->mkdir(dir, dentry, mode);
    
    if (ret < 0) {
        dput(dentry);
    }
    
    dput(parent);
    
    return ret;
}

int vfs_rmdir(const char *pathname) {
    struct dentry *dentry;
    struct dentry *parent;
    struct inode *dir;
    int ret;
    
    dentry = vfs_lookup(pathname);
    if (!dentry) {
        return -ENOENT;
    }

    /* Disallow removing mounted directories (e.g. /dev, /proc if mounted). */
    if (vfs_is_mountpoint(dentry)) {
        dput(dentry);
        return -EBUSY;
    }
    
    if (!dentry->d_inode || !S_ISDIR(dentry->d_inode->i_mode)) {
        dput(dentry);
        return -ENOTDIR;
    }
    
    parent = dentry->d_parent;
    if (!parent) {
        dput(dentry);
        return -EINVAL;
    }
    
    dir = parent->d_inode;
    ret = vfs_check_permission(dir, MAY_WRITE | MAY_EXEC);
    if (ret < 0) {
        dput(dentry);
        return ret;
    }
    if (!dir->i_op || !dir->i_op->rmdir) {
        dput(dentry);
        return -EROFS;
    }
    
    ret = dir->i_op->rmdir(dir, dentry);
    
    dput(dentry);
    
    return ret;
}

int vfs_unlink(const char *pathname) {
    struct dentry *dentry;
    struct dentry *parent;
    struct inode *dir;
    int ret;
    
    dentry = vfs_lookup(pathname);
    if (!dentry) {
        return -ENOENT;
    }
    
    if (!dentry->d_inode) {
        dput(dentry);
        return -ENOENT;
    }
    
    if (S_ISDIR(dentry->d_inode->i_mode)) {
        dput(dentry);
        return -EISDIR;
    }
    
    parent = dentry->d_parent;
    if (!parent) {
        dput(dentry);
        return -EINVAL;
    }
    
    dir = parent->d_inode;
    ret = vfs_check_permission(dir, MAY_WRITE | MAY_EXEC);
    if (ret < 0) {
        dput(dentry);
        return ret;
    }
    if (!dir->i_op || !dir->i_op->unlink) {
        dput(dentry);
        return -EROFS;
    }
    
    ret = dir->i_op->unlink(dir, dentry);
    
    dput(dentry);
    
    return ret;
}

/* ==========================================================================
 * Stat Operations
 * ========================================================================== */

int vfs_stat(const char *pathname, struct kstat *stat) {
    struct dentry *dentry;
    struct inode *inode;
    
    dentry = vfs_lookup(pathname);
    if (!dentry) {
        return -ENOENT;
    }
    
    inode = dentry->d_inode;
    if (!inode) {
        dput(dentry);
        return -ENOENT;
    }
    
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
    
    dput(dentry);
    
    return 0;
}

int vfs_fstat(struct file *file, struct kstat *stat) {
    struct inode *inode;
    
    if (!file || !file->f_dentry) {
        return -EBADF;
    }
    
    inode = file->f_dentry->d_inode;
    if (!inode) {
        return -EBADF;
    }
    
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

/* ==========================================================================
 * Generic Operations
 * ========================================================================== */

loff_t generic_file_llseek(struct file *file, loff_t offset, int whence) {
    struct inode *inode = file->f_dentry->d_inode;
    loff_t new_pos;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = file->f_pos + offset;
            break;
        case SEEK_END:
            new_pos = inode->i_size + offset;
            break;
        default:
            return -EINVAL;
    }
    
    if (new_pos < 0) {
        return -EINVAL;
    }
    
    file->f_pos = new_pos;
    
    return new_pos;
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void vfs_init(void) {
    printk(KERN_INFO "Initializing VFS...\n");
    
    /* Create caches */
    inode_cache = kmem_cache_create("inode_cache", sizeof(struct inode),
                                    sizeof(void *), 0, NULL);
    dentry_cache = kmem_cache_create("dentry_cache", sizeof(struct dentry),
                                     sizeof(void *), 0, NULL);
    file_cache = kmem_cache_create("file_cache", sizeof(struct file),
                                   sizeof(void *), 0, NULL);
    
    if (!inode_cache || !dentry_cache || !file_cache) {
        panic("Failed to create VFS caches");
    }

    /* Initialize inode hash/list structures before first use. */
    inode_cache_init();
    
    printk(KERN_INFO "VFS initialized\n");
}