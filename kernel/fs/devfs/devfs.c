/*
 * Obelisk OS - Device Filesystem
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <fs/file.h>
#include <obelisk/bootinfo.h>
#include <mm/kmalloc.h>
#include <mm/vmm.h>
#include <arch/mmu.h>
#include <arch/cpu.h>
#include <proc/scheduler.h>

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

char devfs_console_getc(void);
void devfs_console_flush_input(void);

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
        char c = devfs_console_getc();
        if (c == 0x03) {
            devfs_console_flush_input();
            return (i > 0) ? (ssize_t)i : -EINTR;
        }
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

/* Linux fbdev compatibility ioctls used by Xorg/fbdev tooling */
#define FBIOGET_VSCREENINFO 0x4600U
#define FBIOGET_FSCREENINFO 0x4602U

struct fb_bitfield_compat {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo_compat {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield_compat red;
    struct fb_bitfield_compat green;
    struct fb_bitfield_compat blue;
    struct fb_bitfield_compat transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
} __packed;

struct fb_fix_screeninfo_compat {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
} __packed;

struct fb_dev_state {
    bool ready;
    uint8_t *virt;
    uint64_t phys;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    size_t bytes;
};

static struct fb_dev_state g_fb0;

static ssize_t fb0_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    size_t off;
    size_t n;
    (void)file;
    if (!g_fb0.ready || !buf || !pos) return -EINVAL;
    if (*pos < 0) return -EINVAL;
    off = (size_t)(*pos);
    if (off >= g_fb0.bytes) return 0;
    n = g_fb0.bytes - off;
    if (n > count) n = count;
    memcpy(buf, g_fb0.virt + off, n);
    *pos += (loff_t)n;
    return (ssize_t)n;
}

static ssize_t fb0_write(struct file *file, const char *buf, size_t count, loff_t *pos) {
    size_t off;
    size_t n;
    (void)file;
    if (!g_fb0.ready || !buf || !pos) return -EINVAL;
    if (*pos < 0) return -EINVAL;
    off = (size_t)(*pos);
    if (off >= g_fb0.bytes) return -ENOSPC;
    n = g_fb0.bytes - off;
    if (n > count) n = count;
    memcpy(g_fb0.virt + off, buf, n);
    *pos += (loff_t)n;
    return (ssize_t)n;
}

static int fb0_ioctl(struct file *file, unsigned int request, unsigned long arg) {
    (void)file;
    if (!g_fb0.ready) return -ENODEV;
    if (arg == 0) return -EINVAL;
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo_compat v;
        memset(&v, 0, sizeof(v));
        v.xres = g_fb0.width;
        v.yres = g_fb0.height;
        v.xres_virtual = g_fb0.width;
        v.yres_virtual = g_fb0.height;
        v.bits_per_pixel = g_fb0.bpp;
        v.width = g_fb0.width;
        v.height = g_fb0.height;
        if (g_fb0.bpp == 32) {
            v.red.offset = 16; v.red.length = 8;
            v.green.offset = 8; v.green.length = 8;
            v.blue.offset = 0; v.blue.length = 8;
            v.transp.offset = 24; v.transp.length = 8;
        }
        return vmm_copy_to_user((void *)(uintptr_t)arg, &v, sizeof(v)) < 0 ? -EFAULT : 0;
    }
    if (request == FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo_compat f;
        memset(&f, 0, sizeof(f));
        strncpy(f.id, "obeliskfb0", sizeof(f.id) - 1);
        f.smem_start = g_fb0.phys;
        f.smem_len = (uint32_t)g_fb0.bytes;
        f.type = 0;      /* FB_TYPE_PACKED_PIXELS */
        f.visual = 2;    /* FB_VISUAL_TRUECOLOR */
        f.line_length = g_fb0.pitch;
        return vmm_copy_to_user((void *)(uintptr_t)arg, &f, sizeof(f)) < 0 ? -EFAULT : 0;
    }
    return -ENOTTY;
}

static const struct file_operations fb0_fops = {
    .read = fb0_read,
    .write = fb0_write,
    .ioctl = fb0_ioctl,
};

/* Minimal evdev compatibility for Xorg/libinput probing */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define SYN_REPORT 0
#define KEY_ESC 1
#define KEY_1 2
#define KEY_2 3
#define KEY_3 4
#define KEY_4 5
#define KEY_5 6
#define KEY_6 7
#define KEY_7 8
#define KEY_8 9
#define KEY_9 10
#define KEY_0 11
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_ENTER 28
#define KEY_TAB 15
#define KEY_Q 16
#define KEY_W 17
#define KEY_E 18
#define KEY_R 19
#define KEY_T 20
#define KEY_Y 21
#define KEY_U 22
#define KEY_I 23
#define KEY_O 24
#define KEY_P 25
#define KEY_LEFTBRACE 26
#define KEY_RIGHTBRACE 27
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_F 33
#define KEY_G 34
#define KEY_H 35
#define KEY_J 36
#define KEY_K 37
#define KEY_L 38
#define KEY_SEMICOLON 39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE 41
#define KEY_BACKSLASH 43
#define KEY_Z 44
#define KEY_X 45
#define KEY_C 46
#define KEY_V 47
#define KEY_B 48
#define KEY_N 49
#define KEY_M 50
#define KEY_COMMA 51
#define KEY_DOT 52
#define KEY_SLASH 53
#define KEY_SPACE 57
#define KEY_BACKSPACE 14
#define KEY_LEFTCTRL 29
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_LEFTALT 56
#define KEY_CAPSLOCK 58
#define KEY_F1 59
#define KEY_F2 60
#define KEY_F3 61
#define KEY_F4 62
#define KEY_F5 63
#define KEY_F6 64
#define KEY_F7 65
#define KEY_F8 66
#define KEY_F9 67
#define KEY_F10 68
#define KEY_F11 87
#define KEY_F12 88
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT 100
#define KEY_HOME 102
#define KEY_UP 103
#define KEY_PAGEUP 104
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_END 107
#define KEY_DOWN 108
#define KEY_PAGEDOWN 109
#define KEY_INSERT 110
#define KEY_DELETE 111

#define REL_X 0
#define REL_Y 1
#define BTN_LEFT 272
#define BTN_RIGHT 273
#define BTN_MIDDLE 274

#define EVIOCGVERSION 0x80044501U
#define EVIOCGID      0x80084502U
#define EVIOCGRAB     0x40044590U

#define EVIOC_TYPE(req) (((req) >> 8) & 0xFFU)
#define EVIOC_NR(req)   ((req) & 0xFFU)
#define EVIOC_SIZE(req) (((req) >> 16) & 0x3FFFU)
#define EVIOC_DIR(req)  (((req) >> 30) & 0x3U)
#define EVIOC_READ      2U

#define I8042_DATA_PORT     0x60
#define I8042_STATUS_PORT   0x64
#define I8042_CMD_PORT      0x64
#define I8042_STAT_OBF      0x01
#define I8042_STAT_IBF      0x02
#define I8042_STAT_AUX      0x20
#define I8042_CMD_READ_CFG  0x20
#define I8042_CMD_WRITE_CFG 0x60
#define I8042_CMD_DISABLE_KBD 0xAD
#define I8042_CMD_ENABLE_KBD  0xAE
#define I8042_CMD_DISABLE_AUX 0xA7
#define I8042_CMD_ENABLE_AUX  0xA8
#define I8042_CMD_SELF_TEST   0xAA
#define I8042_CMD_TEST_KBD    0xAB
#define I8042_CMD_TEST_AUX    0xA9
#define I8042_CMD_WRITE_AUX   0xD4

#define INPUT_EVENT_QUEUE_CAP 256
#define MICE_PKT_QUEUE_CAP    256
#define CONSOLE_INPUT_QUEUE_CAP 512
#define INPUT_WAIT_TIMEOUT_NS 500000000ULL

struct input_id_compat {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
} __packed;

struct input_event_compat {
    int64_t sec;
    int64_t usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
} __packed;

struct input_event_queue {
    struct input_event_compat events[INPUT_EVENT_QUEUE_CAP];
    volatile uint32_t head;
    volatile uint32_t tail;
};

struct mice_pkt {
    uint8_t b[3];
};

static struct input_event_queue g_kbd_q;
static struct input_event_queue g_mouse_q;
static char g_console_in_q[CONSOLE_INPUT_QUEUE_CAP];
static volatile uint32_t g_console_in_head;
static volatile uint32_t g_console_in_tail;
static struct mice_pkt g_mice_q[MICE_PKT_QUEUE_CAP];
static volatile uint32_t g_mice_head;
static volatile uint32_t g_mice_tail;
static bool g_kbd_e0_prefix;
static bool g_kbd_f0_prefix;
static bool g_kbd_use_set2;
static bool g_kbd_shift;
static bool g_kbd_ctrl;
static bool g_kbd_capslock;
static uint8_t g_mouse_pkt[3];
static uint8_t g_mouse_pkt_idx;
static bool g_mouse_left;
static bool g_mouse_right;
static bool g_mouse_middle;
static unsigned long g_kbd_drop_count;
static unsigned long g_mouse_drop_count;
static unsigned long g_mice_drop_count;

static uint64_t irq_save_flags(void) {
    uint64_t flags = read_flags();
    cli();
    return flags;
}

static void irq_restore_flags(uint64_t flags) {
    write_flags(flags);
}

static int input_queue_push(struct input_event_queue *q, const struct input_event_compat *ev,
                            unsigned long *drop_counter) {
    uint64_t flags = irq_save_flags();
    uint32_t next = (q->head + 1U) % INPUT_EVENT_QUEUE_CAP;
    if (next == q->tail) {
        if (drop_counter) {
            (*drop_counter)++;
        }
        irq_restore_flags(flags);
        return -ENOSPC;
    }
    q->events[q->head] = *ev;
    q->head = next;
    irq_restore_flags(flags);
    return 0;
}

static int input_queue_pop(struct input_event_queue *q, struct input_event_compat *ev) {
    uint64_t flags = irq_save_flags();
    if (q->tail == q->head) {
        irq_restore_flags(flags);
        return -EAGAIN;
    }
    *ev = q->events[q->tail];
    q->tail = (q->tail + 1U) % INPUT_EVENT_QUEUE_CAP;
    irq_restore_flags(flags);
    return 0;
}

static void emit_input_event(struct input_event_queue *q, uint16_t type, uint16_t code, int32_t value) {
    struct input_event_compat ev;
    uint64_t now_ns = get_time_ns();
    ev.sec = (int64_t)(now_ns / 1000000000ULL);
    ev.usec = (int64_t)((now_ns % 1000000000ULL) / 1000ULL);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (q == &g_kbd_q) {
        (void)input_queue_push(q, &ev, &g_kbd_drop_count);
    } else if (q == &g_mouse_q) {
        (void)input_queue_push(q, &ev, &g_mouse_drop_count);
    } else {
        (void)input_queue_push(q, &ev, NULL);
    }
}

static int mice_queue_push(const uint8_t pkt[3]) {
    uint64_t flags = irq_save_flags();
    uint32_t next = (g_mice_head + 1U) % MICE_PKT_QUEUE_CAP;
    if (next == g_mice_tail) {
        g_mice_drop_count++;
        irq_restore_flags(flags);
        return -ENOSPC;
    }
    g_mice_q[g_mice_head].b[0] = pkt[0];
    g_mice_q[g_mice_head].b[1] = pkt[1];
    g_mice_q[g_mice_head].b[2] = pkt[2];
    g_mice_head = next;
    irq_restore_flags(flags);
    return 0;
}

static int console_input_push_char(char c) {
    uint64_t flags = irq_save_flags();
    uint32_t next = (g_console_in_head + 1U) % CONSOLE_INPUT_QUEUE_CAP;
    if (next == g_console_in_tail) {
        g_kbd_drop_count++;
        irq_restore_flags(flags);
        return -ENOSPC;
    }
    g_console_in_q[g_console_in_head] = c;
    g_console_in_head = next;
    irq_restore_flags(flags);
    return 0;
}

static int console_input_pop_char_nonblock(void) {
    int out;
    uint64_t flags = irq_save_flags();
    if (g_console_in_tail == g_console_in_head) {
        irq_restore_flags(flags);
        return -EAGAIN;
    }
    out = (unsigned char)g_console_in_q[g_console_in_tail];
    g_console_in_tail = (g_console_in_tail + 1U) % CONSOLE_INPUT_QUEUE_CAP;
    irq_restore_flags(flags);
    return out;
}

static uint16_t ps2_set1_keycode(uint8_t sc, bool e0);
static uint16_t ps2_set2_keycode(uint8_t sc, bool e0);

static void console_push_esc_seq(const char *s) {
    if (!s) return;
    while (*s) {
        (void)console_input_push_char(*s++);
    }
}

static char ps2_keycode_to_ascii(uint16_t code) {
    bool shift = g_kbd_shift;
    bool upper = (shift ^ g_kbd_capslock);
    switch (code) {
        case KEY_A: return upper ? 'A' : 'a';
        case KEY_B: return upper ? 'B' : 'b';
        case KEY_C: return upper ? 'C' : 'c';
        case KEY_D: return upper ? 'D' : 'd';
        case KEY_E: return upper ? 'E' : 'e';
        case KEY_F: return upper ? 'F' : 'f';
        case KEY_G: return upper ? 'G' : 'g';
        case KEY_H: return upper ? 'H' : 'h';
        case KEY_I: return upper ? 'I' : 'i';
        case KEY_J: return upper ? 'J' : 'j';
        case KEY_K: return upper ? 'K' : 'k';
        case KEY_L: return upper ? 'L' : 'l';
        case KEY_M: return upper ? 'M' : 'm';
        case KEY_N: return upper ? 'N' : 'n';
        case KEY_O: return upper ? 'O' : 'o';
        case KEY_P: return upper ? 'P' : 'p';
        case KEY_Q: return upper ? 'Q' : 'q';
        case KEY_R: return upper ? 'R' : 'r';
        case KEY_S: return upper ? 'S' : 's';
        case KEY_T: return upper ? 'T' : 't';
        case KEY_U: return upper ? 'U' : 'u';
        case KEY_V: return upper ? 'V' : 'v';
        case KEY_W: return upper ? 'W' : 'w';
        case KEY_X: return upper ? 'X' : 'x';
        case KEY_Y: return upper ? 'Y' : 'y';
        case KEY_Z: return upper ? 'Z' : 'z';
        case KEY_1: return shift ? '!' : '1';
        case KEY_2: return shift ? '@' : '2';
        case KEY_3: return shift ? '#' : '3';
        case KEY_4: return shift ? '$' : '4';
        case KEY_5: return shift ? '%' : '5';
        case KEY_6: return shift ? '^' : '6';
        case KEY_7: return shift ? '&' : '7';
        case KEY_8: return shift ? '*' : '8';
        case KEY_9: return shift ? '(' : '9';
        case KEY_0: return shift ? ')' : '0';
        case KEY_MINUS: return shift ? '_' : '-';
        case KEY_EQUAL: return shift ? '+' : '=';
        case KEY_LEFTBRACE: return shift ? '{' : '[';
        case KEY_RIGHTBRACE: return shift ? '}' : ']';
        case KEY_BACKSLASH: return shift ? '|' : '\\';
        case KEY_SEMICOLON: return shift ? ':' : ';';
        case KEY_APOSTROPHE: return shift ? '"' : '\'';
        case KEY_GRAVE: return shift ? '~' : '`';
        case KEY_COMMA: return shift ? '<' : ',';
        case KEY_DOT: return shift ? '>' : '.';
        case KEY_SLASH: return shift ? '?' : '/';
        case KEY_SPACE: return ' ';
        case KEY_TAB: return '\t';
        case KEY_ENTER: return '\n';
        case KEY_BACKSPACE: return '\b';
        default: return 0;
    }
}

static void ps2_kbd_feed_byte(uint8_t data, bool emit_events) {
    bool released;
    uint8_t sc;
    uint16_t code;

    /* Ignore controller/keyboard response bytes and noise. */
    if (data == 0xFA || data == 0xFE || data == 0xEE || data == 0x00 || data == 0xFF || data == 0xAA) {
        return;
    }
    if (data == 0xF0 && g_kbd_use_set2) {
        g_kbd_f0_prefix = true;
        return;
    }
    if (data == 0xE1) {
        g_kbd_e0_prefix = false;
        g_kbd_f0_prefix = false;
        return;
    }
    if (data == 0xE0) {
        g_kbd_e0_prefix = true;
        return;
    }

    code = 0;
    released = false;
    if (g_kbd_use_set2) {
        code = ps2_set2_keycode(data, g_kbd_e0_prefix);
        released = g_kbd_f0_prefix;
    } else {
        released = (data & 0x80U) != 0;
        sc = (uint8_t)(data & 0x7FU);
        code = ps2_set1_keycode(sc, g_kbd_e0_prefix);
    }
    g_kbd_e0_prefix = false;
    g_kbd_f0_prefix = false;
    if (code == 0) return;
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        g_kbd_shift = !released;
    } else if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
        g_kbd_ctrl = !released;
    } else if (code == KEY_CAPSLOCK && !released) {
        g_kbd_capslock = !g_kbd_capslock;
    } else if (!released) {
        if (code == KEY_UP) {
            (void)console_input_push_char(0x1B);
            (void)console_input_push_char('[');
            (void)console_input_push_char('A');
        } else if (code == KEY_DOWN) {
            (void)console_input_push_char(0x1B);
            (void)console_input_push_char('[');
            (void)console_input_push_char('B');
        } else if (code == KEY_RIGHT) {
            (void)console_input_push_char(0x1B);
            (void)console_input_push_char('[');
            (void)console_input_push_char('C');
        } else if (code == KEY_LEFT) {
            (void)console_input_push_char(0x1B);
            (void)console_input_push_char('[');
            (void)console_input_push_char('D');
        } else if (code == KEY_HOME) {
            console_push_esc_seq("\x1B[H");
        } else if (code == KEY_END) {
            console_push_esc_seq("\x1B[F");
        } else if (code == KEY_INSERT) {
            console_push_esc_seq("\x1B[2~");
        } else if (code == KEY_DELETE) {
            console_push_esc_seq("\x1B[3~");
        } else if (code == KEY_PAGEUP) {
            console_push_esc_seq("\x1B[5~");
        } else if (code == KEY_PAGEDOWN) {
            console_push_esc_seq("\x1B[6~");
        } else if (code == KEY_F1) {
            console_push_esc_seq("\x1BOP");
        } else if (code == KEY_F2) {
            console_push_esc_seq("\x1BOQ");
        } else if (code == KEY_F3) {
            console_push_esc_seq("\x1BOR");
        } else if (code == KEY_F4) {
            console_push_esc_seq("\x1BOS");
        } else if (code == KEY_F5) {
            console_push_esc_seq("\x1B[15~");
        } else if (code == KEY_F6) {
            console_push_esc_seq("\x1B[17~");
        } else if (code == KEY_F7) {
            console_push_esc_seq("\x1B[18~");
        } else if (code == KEY_F8) {
            console_push_esc_seq("\x1B[19~");
        } else if (code == KEY_F9) {
            console_push_esc_seq("\x1B[20~");
        } else if (code == KEY_F10) {
            console_push_esc_seq("\x1B[21~");
        } else if (code == KEY_F11) {
            console_push_esc_seq("\x1B[23~");
        } else if (code == KEY_F12) {
            console_push_esc_seq("\x1B[24~");
        } else {
            char out = ps2_keycode_to_ascii(code);
            if (out != 0) {
                if (g_kbd_ctrl && out >= 'a' && out <= 'z') {
                    out = (char)(out - 'a' + 1);
                } else if (g_kbd_ctrl && out >= 'A' && out <= 'Z') {
                    out = (char)(out - 'A' + 1);
                }
                (void)console_input_push_char(out);
            }
        }
    }

    if (emit_events) {
        emit_input_event(&g_kbd_q, EV_KEY, code, released ? 0 : 1);
        emit_input_event(&g_kbd_q, EV_SYN, SYN_REPORT, 0);
    }
}

static int mice_queue_pop(uint8_t pkt[3]);

static int wait_for_input_event(struct input_event_queue *q, struct input_event_compat *ev,
                                struct file *file) {
    int ret;
    uint64_t start_ns;
    uint64_t now_ns;
    if (!q || !ev) return -EINVAL;
    ret = input_queue_pop(q, ev);
    if (ret == 0) return 0;
    if (!file || (file->f_flags & O_NONBLOCK)) return -EAGAIN;
    start_ns = get_time_ns();
    for (;;) {
        scheduler_yield();
        ret = input_queue_pop(q, ev);
        if (ret == 0) return 0;
        now_ns = get_time_ns();
        if (now_ns - start_ns > INPUT_WAIT_TIMEOUT_NS) {
            return -EAGAIN;
        }
    }
}

static int wait_for_mice_pkt(uint8_t pkt[3], struct file *file) {
    int ret;
    uint64_t start_ns;
    uint64_t now_ns;
    if (!pkt) return -EINVAL;
    ret = mice_queue_pop(pkt);
    if (ret == 0) return 0;
    if (!file || (file->f_flags & O_NONBLOCK)) return -EAGAIN;
    start_ns = get_time_ns();
    for (;;) {
        scheduler_yield();
        ret = mice_queue_pop(pkt);
        if (ret == 0) return 0;
        now_ns = get_time_ns();
        if (now_ns - start_ns > INPUT_WAIT_TIMEOUT_NS) {
            return -EAGAIN;
        }
    }
}

static int mice_queue_pop(uint8_t pkt[3]) {
    uint64_t flags = irq_save_flags();
    if (g_mice_tail == g_mice_head) {
        irq_restore_flags(flags);
        return -EAGAIN;
    }
    pkt[0] = g_mice_q[g_mice_tail].b[0];
    pkt[1] = g_mice_q[g_mice_tail].b[1];
    pkt[2] = g_mice_q[g_mice_tail].b[2];
    g_mice_tail = (g_mice_tail + 1U) % MICE_PKT_QUEUE_CAP;
    irq_restore_flags(flags);
    return 0;
}

static void bit_set_u8(uint8_t *buf, size_t len, uint16_t bit) {
    size_t idx = (size_t)bit / 8U;
    uint8_t mask = (uint8_t)(1U << (bit & 7U));
    if (!buf || idx >= len) {
        return;
    }
    buf[idx] |= mask;
}

static uint16_t ps2_set1_keycode(uint8_t sc, bool e0) {
    if (e0) {
        switch (sc) {
            case 0x1D: return KEY_RIGHTCTRL;
            case 0x38: return KEY_RIGHTALT;
            case 0x47: return KEY_HOME;
            case 0x49: return KEY_PAGEUP;
            case 0x48: return KEY_UP;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x4F: return KEY_END;
            case 0x50: return KEY_DOWN;
            case 0x51: return KEY_PAGEDOWN;
            case 0x52: return KEY_INSERT;
            case 0x53: return KEY_DELETE;
            default: return 0;
        }
    }
    switch (sc) {
        case 0x01: return KEY_ESC;
        case 0x02: return KEY_1;
        case 0x03: return KEY_2;
        case 0x04: return KEY_3;
        case 0x05: return KEY_4;
        case 0x06: return KEY_5;
        case 0x07: return KEY_6;
        case 0x08: return KEY_7;
        case 0x09: return KEY_8;
        case 0x0A: return KEY_9;
        case 0x0B: return KEY_0;
        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUAL;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;
        case 0x10: return KEY_Q;
        case 0x11: return KEY_W;
        case 0x12: return KEY_E;
        case 0x13: return KEY_R;
        case 0x14: return KEY_T;
        case 0x15: return KEY_Y;
        case 0x16: return KEY_U;
        case 0x17: return KEY_I;
        case 0x18: return KEY_O;
        case 0x19: return KEY_P;
        case 0x1A: return KEY_LEFTBRACE;
        case 0x1B: return KEY_RIGHTBRACE;
        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_LEFTCTRL;
        case 0x1E: return KEY_A;
        case 0x1F: return KEY_S;
        case 0x20: return KEY_D;
        case 0x21: return KEY_F;
        case 0x22: return KEY_G;
        case 0x23: return KEY_H;
        case 0x24: return KEY_J;
        case 0x25: return KEY_K;
        case 0x26: return KEY_L;
        case 0x27: return KEY_SEMICOLON;
        case 0x28: return KEY_APOSTROPHE;
        case 0x29: return KEY_GRAVE;
        case 0x2A: return KEY_LEFTSHIFT;
        case 0x2B: return KEY_BACKSLASH;
        case 0x2C: return KEY_Z;
        case 0x2D: return KEY_X;
        case 0x2E: return KEY_C;
        case 0x2F: return KEY_V;
        case 0x30: return KEY_B;
        case 0x31: return KEY_N;
        case 0x32: return KEY_M;
        case 0x33: return KEY_COMMA;
        case 0x34: return KEY_DOT;
        case 0x35: return KEY_SLASH;
        case 0x36: return KEY_RIGHTSHIFT;
        case 0x38: return KEY_LEFTALT;
        case 0x39: return KEY_SPACE;
        case 0x3A: return KEY_CAPSLOCK;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;
        default: return 0;
    }
}

static uint16_t ps2_set2_keycode(uint8_t sc, bool e0) {
    if (e0) {
        switch (sc) {
            case 0x14: return KEY_RIGHTCTRL;
            case 0x11: return KEY_RIGHTALT;
            case 0x6C: return KEY_HOME;
            case 0x7D: return KEY_PAGEUP;
            case 0x75: return KEY_UP;
            case 0x6B: return KEY_LEFT;
            case 0x74: return KEY_RIGHT;
            case 0x69: return KEY_END;
            case 0x72: return KEY_DOWN;
            case 0x7A: return KEY_PAGEDOWN;
            case 0x70: return KEY_INSERT;
            case 0x71: return KEY_DELETE;
            default: return 0;
        }
    }
    switch (sc) {
        case 0x76: return KEY_ESC;
        case 0x16: return KEY_1;
        case 0x1E: return KEY_2;
        case 0x26: return KEY_3;
        case 0x25: return KEY_4;
        case 0x2E: return KEY_5;
        case 0x36: return KEY_6;
        case 0x3D: return KEY_7;
        case 0x3E: return KEY_8;
        case 0x46: return KEY_9;
        case 0x45: return KEY_0;
        case 0x4E: return KEY_MINUS;
        case 0x55: return KEY_EQUAL;
        case 0x66: return KEY_BACKSPACE;
        case 0x0D: return KEY_TAB;
        case 0x15: return KEY_Q;
        case 0x1D: return KEY_W;
        case 0x24: return KEY_E;
        case 0x2D: return KEY_R;
        case 0x2C: return KEY_T;
        case 0x35: return KEY_Y;
        case 0x3C: return KEY_U;
        case 0x43: return KEY_I;
        case 0x44: return KEY_O;
        case 0x4D: return KEY_P;
        case 0x54: return KEY_LEFTBRACE;
        case 0x5B: return KEY_RIGHTBRACE;
        case 0x5A: return KEY_ENTER;
        case 0x14: return KEY_LEFTCTRL;
        case 0x1C: return KEY_A;
        case 0x1B: return KEY_S;
        case 0x23: return KEY_D;
        case 0x2B: return KEY_F;
        case 0x34: return KEY_G;
        case 0x33: return KEY_H;
        case 0x3B: return KEY_J;
        case 0x42: return KEY_K;
        case 0x4B: return KEY_L;
        case 0x4C: return KEY_SEMICOLON;
        case 0x52: return KEY_APOSTROPHE;
        case 0x0E: return KEY_GRAVE;
        case 0x12: return KEY_LEFTSHIFT;
        case 0x5D: return KEY_BACKSLASH;
        case 0x1A: return KEY_Z;
        case 0x22: return KEY_X;
        case 0x21: return KEY_C;
        case 0x2A: return KEY_V;
        case 0x32: return KEY_B;
        case 0x31: return KEY_N;
        case 0x3A: return KEY_M;
        case 0x41: return KEY_COMMA;
        case 0x49: return KEY_DOT;
        case 0x4A: return KEY_SLASH;
        case 0x59: return KEY_RIGHTSHIFT;
        case 0x11: return KEY_LEFTALT;
        case 0x29: return KEY_SPACE;
        case 0x58: return KEY_CAPSLOCK;
        case 0x05: return KEY_F1;
        case 0x06: return KEY_F2;
        case 0x04: return KEY_F3;
        case 0x0C: return KEY_F4;
        case 0x03: return KEY_F5;
        case 0x0B: return KEY_F6;
        case 0x83: return KEY_F7;
        case 0x0A: return KEY_F8;
        case 0x01: return KEY_F9;
        case 0x09: return KEY_F10;
        case 0x78: return KEY_F11;
        case 0x07: return KEY_F12;
        default: return 0;
    }
}

static int i8042_wait_ibf_clear(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(I8042_STATUS_PORT) & I8042_STAT_IBF) == 0) return 0;
        pause();
    }
    return -ETIMEDOUT;
}

static int i8042_wait_obf_set(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(I8042_STATUS_PORT) & I8042_STAT_OBF) return 0;
        pause();
    }
    return -ETIMEDOUT;
}

static int i8042_write_cmd(uint8_t cmd) {
    if (i8042_wait_ibf_clear() < 0) return -ETIMEDOUT;
    outb(I8042_CMD_PORT, cmd);
    return 0;
}

static int i8042_write_data(uint8_t val) {
    if (i8042_wait_ibf_clear() < 0) return -ETIMEDOUT;
    outb(I8042_DATA_PORT, val);
    return 0;
}

static int i8042_read_data(uint8_t *out) {
    if (!out) return -EINVAL;
    if (i8042_wait_obf_set() < 0) return -ETIMEDOUT;
    *out = inb(I8042_DATA_PORT);
    return 0;
}

static void i8042_flush_output(void) {
    while (inb(I8042_STATUS_PORT) & I8042_STAT_OBF) {
        (void)inb(I8042_DATA_PORT);
    }
}

static int i8042_write_aux(uint8_t val) {
    if (i8042_write_cmd(I8042_CMD_WRITE_AUX) < 0) return -ETIMEDOUT;
    if (i8042_write_data(val) < 0) return -ETIMEDOUT;
    return 0;
}

static void ps2_kbd_irq(uint8_t irq, struct cpu_regs *regs, void *ctx) {
    uint8_t data;
    uint8_t status;
    (void)irq;
    (void)regs;
    (void)ctx;
    status = inb(I8042_STATUS_PORT);
    if ((status & I8042_STAT_OBF) == 0U || (status & I8042_STAT_AUX) != 0U) {
        return;
    }
    data = inb(I8042_DATA_PORT);
    ps2_kbd_feed_byte(data, true);
}

static void ps2_mouse_irq(uint8_t irq, struct cpu_regs *regs, void *ctx) {
    uint8_t b = inb(I8042_DATA_PORT);
    int8_t dx, dy;
    bool left, right, middle;
    (void)irq;
    (void)regs;
    (void)ctx;

    if (g_mouse_pkt_idx == 0 && (b & 0x08U) == 0) {
        return;
    }
    g_mouse_pkt[g_mouse_pkt_idx++] = b;
    if (g_mouse_pkt_idx < 3) return;
    g_mouse_pkt_idx = 0;

    left = (g_mouse_pkt[0] & 0x01U) != 0;
    right = (g_mouse_pkt[0] & 0x02U) != 0;
    middle = (g_mouse_pkt[0] & 0x04U) != 0;
    dx = (int8_t)g_mouse_pkt[1];
    dy = (int8_t)g_mouse_pkt[2];

    if (left != g_mouse_left) {
        g_mouse_left = left;
        emit_input_event(&g_mouse_q, EV_KEY, BTN_LEFT, left ? 1 : 0);
    }
    if (right != g_mouse_right) {
        g_mouse_right = right;
        emit_input_event(&g_mouse_q, EV_KEY, BTN_RIGHT, right ? 1 : 0);
    }
    if (middle != g_mouse_middle) {
        g_mouse_middle = middle;
        emit_input_event(&g_mouse_q, EV_KEY, BTN_MIDDLE, middle ? 1 : 0);
    }
    if (dx != 0) emit_input_event(&g_mouse_q, EV_REL, REL_X, dx);
    if (dy != 0) emit_input_event(&g_mouse_q, EV_REL, REL_Y, -(int32_t)dy);
    emit_input_event(&g_mouse_q, EV_SYN, SYN_REPORT, 0);
    (void)mice_queue_push(g_mouse_pkt);
}

static void ps2_init_devices(void) {
    uint8_t cfg = 0;
    uint8_t tmp = 0;
    int ok = 1;

    if (i8042_write_cmd(I8042_CMD_DISABLE_KBD) < 0) ok = 0;
    if (i8042_write_cmd(I8042_CMD_DISABLE_AUX) < 0) ok = 0;
    i8042_flush_output();

    if (ok && i8042_write_cmd(I8042_CMD_READ_CFG) == 0 && i8042_read_data(&cfg) == 0) {
        cfg &= ~(uint8_t)(0x01U | 0x02U);
        /* Force XT/set1 translation for stable key decoding on VM + bare metal. */
        cfg |= 0x40U;
        (void)i8042_write_cmd(I8042_CMD_WRITE_CFG);
        (void)i8042_write_data(cfg);
        g_kbd_use_set2 = ((cfg & 0x40U) == 0U);
    } else {
        ok = 0;
    }

    if (ok && (i8042_write_cmd(I8042_CMD_SELF_TEST) < 0 || i8042_read_data(&tmp) < 0 || tmp != 0x55U)) {
        ok = 0;
    }

    if (!ok) {
        printk(KERN_WARNING "devfs: i8042 not ready, keeping input fallback path\n");
        return;
    }

    (void)i8042_write_cmd(I8042_CMD_TEST_KBD);
    (void)i8042_read_data(&tmp);
    (void)i8042_write_cmd(I8042_CMD_TEST_AUX);
    (void)i8042_read_data(&tmp);

    (void)i8042_write_cmd(I8042_CMD_ENABLE_KBD);
    (void)i8042_write_cmd(I8042_CMD_ENABLE_AUX);

    cfg |= (uint8_t)(0x01U | 0x02U | 0x40U);
    (void)i8042_write_cmd(I8042_CMD_WRITE_CFG);
    (void)i8042_write_data(cfg);

    (void)i8042_write_data(0xF4);
    (void)i8042_read_data(&tmp);
    (void)i8042_write_aux(0xF4);
    (void)i8042_read_data(&tmp);

    if (irq_register_handler(1, ps2_kbd_irq, NULL) == 0) {
        irq_enable(1);
    }
    if (irq_register_handler(12, ps2_mouse_irq, NULL) == 0) {
        irq_enable(2);   /* Cascade for slave PIC */
        irq_enable(12);
    }

    printk(KERN_INFO "devfs: PS/2 input backend enabled (IRQ1/IRQ12)\n");
}

static ssize_t event0_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    struct input_event_compat ev;
    (void)file;
    (void)pos;
    if (!buf) return -EFAULT;
    if (count < sizeof(ev)) return -EINVAL;
    if (wait_for_input_event(&g_kbd_q, &ev, file) < 0) return -EAGAIN;
    memcpy(buf, &ev, sizeof(ev));
    return (ssize_t)sizeof(ev);
}

static int event_ioctl_common(const char *name, bool is_mouse, unsigned int request, unsigned long arg) {
    struct input_id_compat id;
    uint32_t ver = 0x010001U;
    if (request == EVIOCGRAB) return 0;
    if (arg == 0) return -EINVAL;

    if (request == EVIOCGVERSION) {
        return vmm_copy_to_user((void *)(uintptr_t)arg, &ver, sizeof(ver)) < 0 ? -EFAULT : 0;
    }
    if (request == EVIOCGID) {
        memset(&id, 0, sizeof(id));
        id.bustype = 0x0011; /* BUS_HOST */
        id.vendor = 0x1D6B;
        id.product = 0x0001;
        id.version = 1;
        return vmm_copy_to_user((void *)(uintptr_t)arg, &id, sizeof(id)) < 0 ? -EFAULT : 0;
    }
    if (EVIOC_TYPE(request) == 'E' && EVIOC_DIR(request) == EVIOC_READ) {
        if (EVIOC_NR(request) == 0x06) { /* EVIOCGNAME(len) */
            size_t n = strlen(name) + 1;
            size_t sz = EVIOC_SIZE(request);
            if (sz == 0) return -EINVAL;
            if (n > sz) n = sz;
            return vmm_copy_to_user((void *)(uintptr_t)arg, name, n) < 0 ? -EFAULT : 0;
        }
        if (EVIOC_NR(request) >= 0x20 && EVIOC_NR(request) < 0x40) { /* EVIOCGBIT */
            uint8_t bits[32];
            size_t sz = EVIOC_SIZE(request);
            uint8_t nr = (uint8_t)EVIOC_NR(request);
            memset(bits, 0, sizeof(bits));
            if (nr == 0x20) {
                bit_set_u8(bits, sizeof(bits), EV_SYN);
                bit_set_u8(bits, sizeof(bits), EV_KEY);
                if (is_mouse) {
                    bit_set_u8(bits, sizeof(bits), EV_REL);
                }
            } else if (nr == (uint8_t)(0x20 + EV_KEY)) {
                if (is_mouse) {
                    bit_set_u8(bits, sizeof(bits), BTN_LEFT);
                    bit_set_u8(bits, sizeof(bits), BTN_RIGHT);
                    bit_set_u8(bits, sizeof(bits), BTN_MIDDLE);
                } else {
                    uint16_t keys[] = {
                        KEY_ESC, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
                        KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE, KEY_TAB, KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T,
                        KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_ENTER,
                        KEY_LEFTCTRL, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
                        KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE, KEY_LEFTSHIFT, KEY_BACKSLASH,
                        KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH,
                        KEY_RIGHTSHIFT, KEY_LEFTALT, KEY_SPACE, KEY_CAPSLOCK, KEY_UP, KEY_LEFT, KEY_RIGHT, KEY_DOWN
                    };
                    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
                        bit_set_u8(bits, sizeof(bits), keys[i]);
                    }
                }
            } else if (nr == (uint8_t)(0x20 + EV_REL) && is_mouse) {
                bit_set_u8(bits, sizeof(bits), REL_X);
                bit_set_u8(bits, sizeof(bits), REL_Y);
            }
            if (sz > sizeof(bits)) sz = sizeof(bits);
            return vmm_copy_to_user((void *)(uintptr_t)arg, bits, sz) < 0 ? -EFAULT : 0;
        }
    }
    return -ENOTTY;
}

static int event0_ioctl(struct file *file, unsigned int request, unsigned long arg) {
    (void)file;
    return event_ioctl_common("Obelisk PS/2 Keyboard", false, request, arg);
}

static const struct file_operations event0_fops = {
    .read = event0_read,
    .ioctl = event0_ioctl,
};

static ssize_t event1_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    struct input_event_compat ev;
    (void)file;
    (void)pos;
    if (!buf) return -EFAULT;
    if (count < sizeof(ev)) return -EINVAL;
    if (wait_for_input_event(&g_mouse_q, &ev, file) < 0) return -EAGAIN;
    memcpy(buf, &ev, sizeof(ev));
    return (ssize_t)sizeof(ev);
}

static int event1_ioctl(struct file *file, unsigned int request, unsigned long arg) {
    (void)file;
    return event_ioctl_common("Obelisk PS/2 Mouse", true, request, arg);
}

static const struct file_operations event1_fops = {
    .read = event1_read,
    .ioctl = event1_ioctl,
};

static ssize_t mice_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    uint8_t pkt[3];
    (void)file;
    (void)pos;
    if (!buf) return -EFAULT;
    if (count < sizeof(pkt)) return -EINVAL;
    if (wait_for_mice_pkt(pkt, file) < 0) return -EAGAIN;
    memcpy(buf, pkt, sizeof(pkt));
    return (ssize_t)sizeof(pkt);
}

static const struct file_operations mice_fops = {
    .read = mice_read,
};

static void devfs_register_fb0(void) {
    const struct obelisk_framebuffer_info *fb = bootinfo_framebuffer();
    uint64_t map_phys;
    uint64_t map_size;
    uint64_t virt_base;
    uint64_t fb_bytes;
    int ret;

    if (!fb || !fb->available || fb->width == 0 || fb->height == 0 || fb->pitch == 0 || fb->bpp == 0) {
        printk(KERN_INFO "devfs: framebuffer not available, skipping /dev/fb0\n");
        return;
    }

    fb_bytes = (uint64_t)fb->pitch * (uint64_t)fb->height;
    if (fb_bytes == 0 || fb_bytes > (uint64_t)SIZE_MAX) {
        printk(KERN_WARNING "devfs: invalid framebuffer size, skipping /dev/fb0\n");
        return;
    }

    map_phys = ALIGN_DOWN(fb->phys_addr, PAGE_SIZE);
    map_size = ALIGN_UP((fb->phys_addr - map_phys) + fb_bytes, PAGE_SIZE);
    virt_base = (uint64_t)PHYS_TO_VIRT(map_phys);
    ret = mmu_map_range(mmu_get_kernel_pt(), virt_base, map_phys, (size_t)map_size, PTE_WRITABLE | PTE_NOCACHE);
    if (ret < 0) {
        printk(KERN_WARNING "devfs: failed to map framebuffer MMIO (ret=%d)\n", ret);
        return;
    }

    g_fb0.ready = true;
    g_fb0.phys = fb->phys_addr;
    g_fb0.width = fb->width;
    g_fb0.height = fb->height;
    g_fb0.pitch = fb->pitch;
    g_fb0.bpp = fb->bpp;
    g_fb0.bytes = (size_t)fb_bytes;
    g_fb0.virt = (uint8_t *)(virt_base + (fb->phys_addr - map_phys));

    devfs_register("fb0", DEV_TYPE_CHAR, MKDEV(29, 0), 0666, &fb0_fops, &g_fb0);
    printk(KERN_INFO "devfs: /dev/fb0 registered (%lux%lu@%ubpp pitch=%lu)\n",
           (unsigned long)g_fb0.width,
           (unsigned long)g_fb0.height,
           (unsigned int)g_fb0.bpp,
           (unsigned long)g_fb0.pitch);
}

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
    devfs_register("event0", DEV_TYPE_CHAR, MKDEV(13, 64), 0666, &event0_fops, NULL);
    devfs_register("event1", DEV_TYPE_CHAR, MKDEV(13, 65), 0666, &event1_fops, NULL);
    devfs_register("mice", DEV_TYPE_CHAR, MKDEV(13, 63), 0666, &mice_fops, NULL);
    ps2_init_devices();
    devfs_register_fb0();
    
    printk(KERN_INFO "devfs initialized\n");
}

int devfs_console_getc_nonblock(void) {
    int c = console_input_pop_char_nonblock();
    if (c < 0 && (inb(I8042_STATUS_PORT) & I8042_STAT_OBF)) {
        uint8_t status = inb(I8042_STATUS_PORT);
        uint8_t data = inb(I8042_DATA_PORT);
        if ((status & I8042_STAT_AUX) == 0U) {
            ps2_kbd_feed_byte(data, false);
            c = console_input_pop_char_nonblock();
        }
    }
    if (c >= 0) {
        return c;
    }
    return uart_getc_nonblock();
}

char devfs_console_getc(void) {
    for (;;) {
        int c = devfs_console_getc_nonblock();
        if (c >= 0) {
            return (char)c;
        }
        console_poll();
        scheduler_yield();
    }
}

void devfs_console_flush_input(void) {
    uint64_t flags = irq_save_flags();
    g_console_in_head = 0;
    g_console_in_tail = 0;
    irq_restore_flags(flags);
    while (uart_getc_nonblock() >= 0) {
        /* Drain UART input too. */
    }
}

unsigned long devfs_input_kbd_drop_count(void) {
    return g_kbd_drop_count;
}

unsigned long devfs_input_mouse_drop_count(void) {
    return g_mouse_drop_count;
}

unsigned long devfs_input_mice_drop_count(void) {
    return g_mice_drop_count;
}

/* Device number helpers */