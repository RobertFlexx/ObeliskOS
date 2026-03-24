/*
 * Obelisk OS - Device Filesystem
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <fs/file.h>
#include <mm/kmalloc.h>

/* Device types */
#define DEV_TYPE_CHAR   1
#define DEV_TYPE_BLOCK  2

/* Device entry */
struct devfs_entry {
    char name[64];
    int type;
    dev_t dev;
    mode_t mode;
    const struct file_operations *fops;
    void *private;
    struct list_head list;
};

/* DevFS superblock info */
struct devfs_sb_info {
    struct list_head devices;
    spinlock_t lock;
};

/* Global device list */
static LIST_HEAD(device_list);

/* DevFS operations */
static struct dentry *devfs_lookup(struct inode *dir, struct dentry *dentry, 
                                   unsigned int flags);
static int devfs_readdir(struct file *file, struct dir_context *ctx);

static const struct inode_operations devfs_dir_inode_ops = {
    .lookup = devfs_lookup,
};

static const struct file_operations devfs_dir_ops = {
    .readdir = devfs_readdir,
};

/* ==========================================================================
 * Device Registration
 * ========================================================================== */

int devfs_register(const char *name, int type, dev_t dev, mode_t mode,
                   const struct file_operations *fops, void *private) {
    struct devfs_entry *entry;
    
    entry = kzalloc(sizeof(struct devfs_entry));
    if (!entry) {
        return -ENOMEM;
    }
    
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->type = type;
    entry->dev = dev;
    entry->mode = mode;
    entry->fops = fops;
    entry->private = private;
    
    list_add(&entry->list, &device_list);
    
    printk(KERN_INFO "devfs: Registered device '%s' (%s, %d:%d)\n",
           name, type == DEV_TYPE_CHAR ? "char" : "block",
           (int)(dev >> 8), (int)(dev & 0xFF));
    
    return 0;
}

int devfs_unregister(const char *name) {
    struct devfs_entry *entry;
    
    list_for_each_entry(entry, &device_list, list) {
        if (strcmp(entry->name, name) == 0) {
            list_del(&entry->list);
            kfree(entry);
            printk(KERN_INFO "devfs: Unregistered device '%s'\n", name);
            return 0;
        }
    }
    
    return -ENOENT;
}

static struct devfs_entry *devfs_find(const char *name) {
    struct devfs_entry *entry;
    
    list_for_each_entry(entry, &device_list, list) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    
    return NULL;
}

/* ==========================================================================
 * DevFS Operations
 * ========================================================================== */

static struct dentry *devfs_lookup(struct inode *dir, struct dentry *dentry,
                                   unsigned int flags) {
    struct devfs_entry *entry;
    struct inode *inode;
    
    (void)dir;
    (void)flags;
    
    entry = devfs_find(dentry->d_name);
    if (!entry) {
        d_add(dentry, NULL);
        return NULL;
    }
    
    /* Create inode for device */
    inode = new_inode(dentry->d_sb);
    if (!inode) {
        return ERR_PTR(-ENOMEM);
    }
    
    inode->i_ino = (ino_t)(uintptr_t)entry;
    inode->i_mode = entry->mode;
    if (entry->type == DEV_TYPE_CHAR) {
        inode->i_mode |= S_IFCHR;
    } else {
        inode->i_mode |= S_IFBLK;
    }
    inode->i_rdev = entry->dev;
    inode->i_fop = entry->fops;
    inode->i_private = entry->private;
    
    d_add(dentry, inode);
    
    return NULL;
}

static int devfs_readdir(struct file *file, struct dir_context *ctx) {
    struct devfs_entry *entry;
    int pos = 0;
    (void)file;
    
    /* Emit . and .. */
    if (ctx->pos == 0) {
        if (ctx->actor(ctx, ".", 1, ctx->pos, 1, DT_DIR)) {
            return 0;
        }
        ctx->pos++;
    }
    
    if (ctx->pos == 1) {
        if (ctx->actor(ctx, "..", 2, ctx->pos, 1, DT_DIR)) {
            return 0;
        }
        ctx->pos++;
    }
    
    /* Emit devices */
    pos = 2;
    list_for_each_entry(entry, &device_list, list) {
        if (pos >= ctx->pos) {
            unsigned char type = entry->type == DEV_TYPE_CHAR ? DT_CHR : DT_BLK;
            if (ctx->actor(ctx, entry->name, strlen(entry->name),
                          pos, (ino_t)(uintptr_t)entry, type)) {
                return 0;
            }
            ctx->pos = pos + 1;
        }
        pos++;
    }
    
    return 0;
}

/* ==========================================================================
 * DevFS Mount
 * ========================================================================== */

static struct super_block *devfs_mount(struct file_system_type *fs_type,
                                       int flags, const char *dev, void *data) {
    struct super_block *sb;
    struct devfs_sb_info *sbi;
    struct inode *root_inode;
    
    (void)fs_type;
    (void)dev;
    (void)data;
    
    sb = kzalloc(sizeof(struct super_block));
    if (!sb) {
        return ERR_PTR(-ENOMEM);
    }
    
    sbi = kzalloc(sizeof(struct devfs_sb_info));
    if (!sbi) {
        kfree(sb);
        return ERR_PTR(-ENOMEM);
    }
    
    INIT_LIST_HEAD(&sbi->devices);
    sbi->lock = (spinlock_t)SPINLOCK_INIT;
    
    sb->s_blocksize = 4096;
    sb->s_magic = 0xDEF5;
    sb->s_flags = flags;
    sb->s_fs_info = sbi;
    sb->s_count.counter = 1;
    sb->s_lock = (spinlock_t)SPINLOCK_INIT;
    
    /* Create root inode */
    root_inode = new_inode(sb);
    if (!root_inode) {
        kfree(sbi);
        kfree(sb);
        return ERR_PTR(-ENOMEM);
    }
    
    root_inode->i_ino = 1;
    root_inode->i_mode = S_IFDIR | 0755;
    root_inode->i_nlink = 2;
    root_inode->i_op = &devfs_dir_inode_ops;
    root_inode->i_fop = &devfs_dir_ops;
    
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        iput(root_inode);
        kfree(sbi);
        kfree(sb);
        return ERR_PTR(-ENOMEM);
    }
    
    return sb;
}

static void devfs_kill_sb(struct super_block *sb) {
    if (sb->s_fs_info) {
        kfree(sb->s_fs_info);
    }
    if (sb->s_root) {
        dput(sb->s_root);
    }
    kfree(sb);
}

static struct file_system_type devfs_fs_type = {
    .name = "devfs",
    .fs_flags = 0,
    .mount = devfs_mount,
    .kill_sb = devfs_kill_sb,
};

/* ==========================================================================
 * Standard Device Operations
 * ========================================================================== */

/* Null device */
static ssize_t null_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    (void)file; (void)buf; (void)count; (void)pos;
    return 0;
}

static ssize_t null_write(struct file *file, const char *buf, size_t count, loff_t *pos) {
    (void)file; (void)buf; (void)pos;
    return count;
}

static const struct file_operations null_fops = {
    .read = null_read,
    .write = null_write,
};

/* Zero device */
static ssize_t zero_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    (void)file; (void)pos;
    memset(buf, 0, count);
    return count;
}

static const struct file_operations zero_fops = {
    .read = zero_read,
    .write = null_write,
};

/* Console device */
static ssize_t console_dev_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    (void)file; (void)pos;
    if (!buf) {
        return -EFAULT;
    }
    if (count == 0) {
        return 0;
    }

    size_t i = 0;
    while (i < count) {
        char c = uart_getc();
        if (c == '\r') {
            c = '\n';
        }
        buf[i++] = c;
        if (c == '\n') {
            break;
        }
    }
    return (ssize_t)i;
}

static ssize_t console_dev_write(struct file *file, const char *buf, size_t count, loff_t *pos) {
    (void)file; (void)pos;
    if (!buf) {
        return -EFAULT;
    }
    console_write(buf, count);
    return count;
}

static const struct file_operations console_fops = {
    .read = console_dev_read,
    .write = console_dev_write,
};

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void devfs_init(void) {
    printk(KERN_INFO "Initializing devfs...\n");
    
    /* Register filesystem */
    register_filesystem(&devfs_fs_type);
    
    /* Register standard devices */
    devfs_register("null", DEV_TYPE_CHAR, MKDEV(1, 3), 0666, &null_fops, NULL);
    devfs_register("zero", DEV_TYPE_CHAR, MKDEV(1, 5), 0666, &zero_fops, NULL);
    devfs_register("console", DEV_TYPE_CHAR, MKDEV(5, 1), 0600, &console_fops, NULL);
    devfs_register("tty", DEV_TYPE_CHAR, MKDEV(5, 0), 0666, &console_fops, NULL);
    
    printk(KERN_INFO "devfs initialized\n");
}

/* Device number helpers */