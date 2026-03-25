/*
 * Obelisk OS - fbinfo utility
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
} __attribute__((packed));

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
} __attribute__((packed));

static inline long syscall3(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile (
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

static __attribute__((used)) void fbinfo_main(void) {
    int fd = open("/dev/fb0", O_RDONLY, 0);
    struct fb_var_screeninfo_compat v;
    struct fb_fix_screeninfo_compat f;

    if (fd < 0) {
        printf("fbinfo: /dev/fb0 not available\n");
        _exit(1);
    }

    if (syscall3(SYS_IOCTL, fd, FBIOGET_VSCREENINFO, (long)&v) < 0) {
        printf("fbinfo: FBIOGET_VSCREENINFO failed\n");
        close(fd);
        _exit(1);
    }
    if (syscall3(SYS_IOCTL, fd, FBIOGET_FSCREENINFO, (long)&f) < 0) {
        printf("fbinfo: FBIOGET_FSCREENINFO failed\n");
        close(fd);
        _exit(1);
    }

    printf("fb0: %ux%u %ubpp pitch=%u\n", v.xres, v.yres, v.bits_per_pixel, f.line_length);
    printf("fb0: size=%u id=%s\n", f.smem_len, f.id);
    close(fd);
    _exit(0);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "andq $-16, %rsp\n"
        "call fbinfo_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}
