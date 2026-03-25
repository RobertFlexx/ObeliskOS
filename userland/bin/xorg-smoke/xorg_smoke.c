/*
 * Obelisk OS - xorg-smoke utility
 * From Axioms, Order.
 */

#include <stddef.h>
#include <stdint.h>

extern int printf(const char *fmt, ...);
extern int open(const char *pathname, int flags, int mode);
extern int close(int fd);
extern void _exit(int status);

#define O_RDONLY 0x0000
#define SYS_IOCTL 16

#define FBIOGET_VSCREENINFO 0x4600UL
#define FBIOGET_FSCREENINFO 0x4602UL
#define EVIOCGVERSION 0x80044501UL
#define EVIOCGID      0x80084502UL
#define TIOCGWINSZ    0x5413UL
#define KDGETMODE     0x4B3BUL
#define VT_GETSTATE   0x5603UL

struct winsize_compat {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

struct vt_stat_compat {
    uint16_t v_active;
    uint16_t v_signal;
    uint16_t v_state;
};

struct input_id_compat {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
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
    uint32_t rest[32];
};

struct fb_fix_screeninfo_compat {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t rest[20];
};

static inline long syscall3(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile(
        "movq %1, %%rax\n"
        "movq %2, %%rdi\n"
        "movq %3, %%rsi\n"
        "movq %4, %%rdx\n"
        "int $0x80\n"
        "movq %%rax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(arg1), "r"(arg2), "r"(arg3)
        : "rax", "rdi", "rsi", "rdx", "memory"
    );
    return ret;
}

static int check_fb(void) {
    int fd = open("/dev/fb0", O_RDONLY, 0);
    struct fb_var_screeninfo_compat v;
    struct fb_fix_screeninfo_compat f;
    if (fd < 0) {
        printf("[fail] /dev/fb0 open\n");
        return -1;
    }
    if (syscall3(SYS_IOCTL, fd, FBIOGET_VSCREENINFO, (long)&v) < 0 ||
        syscall3(SYS_IOCTL, fd, FBIOGET_FSCREENINFO, (long)&f) < 0) {
        printf("[fail] /dev/fb0 ioctl\n");
        close(fd);
        return -1;
    }
    printf("[ok] fb0 %ux%u %ubpp\n", v.xres, v.yres, v.bits_per_pixel);
    close(fd);
    return 0;
}

static int check_event(const char *path, const char *label) {
    int fd = open(path, O_RDONLY, 0);
    uint32_t ver = 0;
    struct input_id_compat id;
    if (fd < 0) {
        printf("[fail] %s open\n", label);
        return -1;
    }
    if (syscall3(SYS_IOCTL, fd, EVIOCGVERSION, (long)&ver) < 0 ||
        syscall3(SYS_IOCTL, fd, EVIOCGID, (long)&id) < 0) {
        printf("[fail] %s ioctl\n", label);
        close(fd);
        return -1;
    }
    printf("[ok] %s ver=0x%x id=%u:%u\n", label, ver, (unsigned)id.vendor, (unsigned)id.product);
    close(fd);
    return 0;
}

static int check_mouse_alias_open(void) {
    int fd = open("/dev/input/mouse0", O_RDONLY, 0);
    if (fd < 0) {
        printf("[fail] mouse0 alias open\n");
        return -1;
    }
    printf("[ok] mouse0 alias open\n");
    close(fd);
    return 0;
}

static int check_vt_stdio(void) {
    struct winsize_compat ws;
    struct vt_stat_compat st;
    int kd = 0;
    if (syscall3(SYS_IOCTL, 0, TIOCGWINSZ, (long)&ws) < 0 ||
        syscall3(SYS_IOCTL, 0, KDGETMODE, (long)&kd) < 0 ||
        syscall3(SYS_IOCTL, 0, VT_GETSTATE, (long)&st) < 0) {
        printf("[fail] tty/vt ioctl on stdin\n");
        return -1;
    }
    printf("[ok] tty cols=%u rows=%u vt=%u kd=%d\n",
           (unsigned)ws.ws_col, (unsigned)ws.ws_row, (unsigned)st.v_active, kd);
    return 0;
}

static __attribute__((used)) void xorg_smoke_main(void) {
    int fails = 0;
    printf("xorg-smoke: probing framebuffer/input/vt...\n");
    if (check_fb() < 0) fails++;
    if (check_event("/dev/input/event0", "event0") < 0) fails++;
    if (check_event("/dev/input/event1", "event1") < 0) fails++;
    if (check_mouse_alias_open() < 0) fails++;
    if (check_vt_stdio() < 0) fails++;
    if (fails == 0) {
        printf("xorg-smoke: PASS\n");
        _exit(0);
    }
    printf("xorg-smoke: FAIL (%d checks)\n", fails);
    _exit(1);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "andq $-16, %rsp\n"
        "call xorg_smoke_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}
