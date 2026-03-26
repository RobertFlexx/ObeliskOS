#include <stddef.h>
#include <stdint.h>

#include "../../lib/native_gui.h"

extern int printf(const char *fmt, ...);
extern int open(const char *pathname, int flags, int mode);
extern long read(int fd, void *buf, unsigned long count);
extern int close(int fd);
extern void _exit(int status);
extern int chdir(const char *path);
extern void *memset(void *s, int c, size_t n);

#define O_RDONLY 0x0000
#define SYS_POLL 7
#define SYS_GETDENTS64 217

struct pollfd_compat { int fd; short events; short revents; } __attribute__((packed));
#define POLLIN 0x0001

struct linux_dirent64_compat {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

static char g_items[128][64];
static int g_count;
static int g_sel;
static char g_status[64];

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "movq %1, %%rax\n"
        "movq %2, %%rdi\n"
        "movq %3, %%rsi\n"
        "movq %4, %%rdx\n"
        "int $0x80\n"
        "movq %%rax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3)
        : "rax", "rdi", "rsi", "rdx", "memory");
    return ret;
}

static void reload_dir(void) {
    int fd = open(".", O_RDONLY, 0);
    char buf[1024];
    g_count = 0;
    if (fd < 0) return;
    for (;;) {
        int nread = (int)syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
        int bpos = 0;
        if (nread <= 0) break;
        while (bpos < nread && g_count < 128) {
            struct linux_dirent64_compat *d = (struct linux_dirent64_compat *)(buf + bpos);
            int j = 0;
            if (d->d_reclen == 0) break;
            if (d->d_name[0] == 0) { bpos += d->d_reclen; continue; }
            while (d->d_name[j] && j < 63) { g_items[g_count][j] = d->d_name[j]; j++; }
            g_items[g_count][j] = 0;
            g_count++;
            bpos += d->d_reclen;
        }
    }
    close(fd);
    if (g_sel >= g_count) g_sel = g_count - 1;
    if (g_sel < 0) g_sel = 0;
    if (g_count == 0) {
        g_status[0] = 'e'; g_status[1] = 'm'; g_status[2] = 'p'; g_status[3] = 't'; g_status[4] = 'y'; g_status[5] = 0;
    } else {
        g_status[0] = 'o'; g_status[1] = 'k'; g_status[2] = 0;
    }
}

static __attribute__((used)) void ofiles_main(void) {
    struct ng_fb fb;
    struct ng_theme th;
    int kfd;
    int dirty = 1;
    if (ng_fb_open(&fb) < 0) {
        printf("ofiles: no /dev/fb0\n");
        _exit(1);
    }
    kfd = open("/dev/input/event0", O_RDONLY, 0);
    if (kfd < 0) {
        ng_fb_close(&fb);
        _exit(2);
    }
    th = ng_theme_ctwm();
    g_status[0] = 'i'; g_status[1] = 'n'; g_status[2] = 'i'; g_status[3] = 't'; g_status[4] = 0;
    reload_dir();
    for (;;) {
        struct pollfd_compat p;
        struct ng_input_event ev;
        int i;
        p.fd = kfd; p.events = POLLIN; p.revents = 0;
        syscall3(SYS_POLL, (long)&p, 1, 40);
        if (p.revents & POLLIN) {
            while (read(kfd, &ev, sizeof(ev)) == (long)sizeof(ev)) {
                if (ev.type != NG_EV_KEY || ev.value != 1) continue;
                if (ev.code == NG_KEY_ESC) { close(kfd); ng_fb_close(&fb); _exit(0); }
                else if (ev.code == NG_KEY_UP) { if (g_sel > 0) { g_sel--; dirty = 1; } }
                else if (ev.code == NG_KEY_DOWN) { if (g_sel + 1 < g_count) { g_sel++; dirty = 1; } }
                else if (ev.code == NG_KEY_ENTER) {
                    if (g_count > 0 && g_items[g_sel][0] == '.' && g_items[g_sel][1] == '.') {
                        chdir("..");
                        reload_dir();
                        dirty = 1;
                    } else if (g_count > 0) {
                        if (chdir(g_items[g_sel]) == 0) {
                            reload_dir();
                            dirty = 1;
                        }
                    }
                }
            }
        }

        if (dirty) {
            ng_fill(&fb, th.desktop_bg);
            ng_rect(&fb, 10, 10, (int)fb.width - 20, (int)fb.height - 20, 0x172230);
            ng_frame_3d(&fb, 10, 10, (int)fb.width - 20, (int)fb.height - 20, th.win_border_light, th.win_border_dark);
            ng_text(&fb, 16, 16, "ofiles (ENTER open, ESC exit)", th.text_fg, 0x172230);
            ng_text(&fb, (int)fb.width - 110, 16, g_status, th.text_dim, 0x172230);
            for (i = 0; i < g_count && i < 36; i++) {
                int y = 34 + i * 10;
                uint32_t bg = (i == g_sel) ? th.win_active_bg : 0x172230;
                ng_rect(&fb, 14, y - 1, (int)fb.width - 28, 9, bg);
                ng_text(&fb, 18, y, g_items[i], th.text_fg, bg);
            }
            dirty = 0;
        }
    }
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "andq $-16, %rsp\n"
        "call ofiles_main\n"
        "mov $0, %edi\n"
        "call _exit\n");
}
