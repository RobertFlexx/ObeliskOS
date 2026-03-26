#include <stddef.h>
#include <stdint.h>

#include "../../lib/native_gui.h"

extern int printf(const char *fmt, ...);
extern int open(const char *pathname, int flags, int mode);
extern long read(int fd, void *buf, unsigned long count);
extern int close(int fd);
extern void _exit(int status);
extern int strcmp(const char *a, const char *b);
extern int chdir(const char *path);
extern int mkdir(const char *pathname, int mode);
extern long write(int fd, const void *buf, unsigned long count);
extern void *memset(void *s, int c, size_t n);
extern size_t strlen(const char *s);
extern char *strcpy(char *d, const char *s);

#define O_RDONLY 0x0000
#define SYS_POLL 7
#define SYS_GETDENTS64 217

struct pollfd_compat {
    int fd;
    short events;
    short revents;
} __attribute__((packed));

#define POLLIN 0x0001

struct linux_dirent64_compat {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

struct win {
    int x, y, w, h;
    int moving;
    int visible;
    const char *title;
};

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

static char g_term_line[128];
static int g_term_len;
static char g_term_out[20][64];
static int g_term_rows;
static char g_files[64][48];
static int g_file_count;

static void term_push(const char *s) {
    int i;
    if (g_term_rows < 20) g_term_rows++;
    for (i = g_term_rows - 1; i > 0; i--) {
        strcpy(g_term_out[i], g_term_out[i - 1]);
    }
    strcpy(g_term_out[0], s);
}

static void do_term_cmd(void) {
    if (strcmp(g_term_line, "help") == 0) {
        term_push("commands: help, clear, files");
    } else if (strcmp(g_term_line, "clear") == 0) {
        g_term_rows = 0;
    } else if (strcmp(g_term_line, "files") == 0) {
        term_push("see right window for directory list");
    } else if (g_term_line[0] == 0) {
        term_push("");
    } else {
        term_push("unknown command");
    }
    g_term_line[0] = 0;
    g_term_len = 0;
}

static void load_files(void) {
    int fd, nread;
    char buf[1024];
    g_file_count = 0;
    fd = open("/", O_RDONLY, 0);
    if (fd < 0) {
        strcpy(g_files[g_file_count++], "<open / failed>");
        return;
    }
    for (;;) {
        int bpos = 0;
        nread = (int)syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
        if (nread <= 0) break;
        while (bpos < nread && g_file_count < 64) {
            struct linux_dirent64_compat *d = (struct linux_dirent64_compat *)(buf + bpos);
            if (d->d_reclen == 0) break;
            if (d->d_name[0] != 0 && !(d->d_name[0] == '.' && d->d_name[1] == 0)) {
                int j = 0;
                while (d->d_name[j] && j < 47) {
                    g_files[g_file_count][j] = d->d_name[j];
                    j++;
                }
                g_files[g_file_count][j] = 0;
                g_file_count++;
            }
            bpos += d->d_reclen;
        }
    }
    close(fd);
}

static void draw_window(struct ng_fb *fb, const struct ng_theme *th, const struct win *w) {
    ng_rect(fb, w->x, w->y, w->w, w->h, 0x2A3644);
    ng_rect(fb, w->x + 1, w->y + 1, w->w - 2, 18, th->win_active_bg);
    ng_frame_3d(fb, w->x, w->y, w->w, w->h, th->win_border_light, th->win_border_dark);
    ng_text(fb, w->x + 6, w->y + 6, w->title, th->text_fg, th->win_active_bg);
}

static void redraw(struct ng_fb *fb, const struct ng_theme *th, struct win *termw, struct win *filesw, int mx, int my) {
    int i;
    char prompt[160];
    ng_fill(fb, th->desktop_bg);
    ng_rect(fb, 0, 0, (int)fb->width, 20, th->panel_bg);
    ng_text(fb, 6, 6, "obwm - Obelisk Retro Desktop", th->text_fg, th->panel_bg);

    draw_window(fb, th, termw);
    draw_window(fb, th, filesw);

    ng_text(fb, termw->x + 8, termw->y + 24, "oterm", th->text_dim, 0x2A3644);
    for (i = 0; i < g_term_rows && i < 14; i++) {
        ng_text(fb, termw->x + 8, termw->y + 40 + i * 10, g_term_out[i], th->text_fg, 0x2A3644);
    }
    strcpy(prompt, "osh$ ");
    if ((size_t)g_term_len < sizeof(prompt) - 6) {
        int p = 5;
        int j = 0;
        while (g_term_line[j] && p < (int)sizeof(prompt) - 1) prompt[p++] = g_term_line[j++];
        prompt[p] = 0;
    }
    ng_text(fb, termw->x + 8, termw->y + termw->h - 18, prompt, th->text_fg, 0x2A3644);

    ng_text(fb, filesw->x + 8, filesw->y + 24, "ofiles: /", th->text_dim, 0x2A3644);
    for (i = 0; i < g_file_count && i < 18; i++) {
        ng_text(fb, filesw->x + 8, filesw->y + 38 + i * 10, g_files[i], th->text_fg, 0x2A3644);
    }

    ng_cursor(fb, mx, my, th->cursor_fg);
}

static int in_title(const struct win *w, int x, int y) {
    return x >= w->x && x < (w->x + w->w) && y >= w->y && y < (w->y + 18);
}

static void clamp_window_to_screen(struct win *w, int sw, int sh) {
    if (w->w > sw - 4) w->w = sw - 4;
    if (w->h > sh - 24) w->h = sh - 24;
    if (w->w < 120) w->w = 120;
    if (w->h < 80) w->h = 80;
    if (w->x < 0) w->x = 0;
    if (w->y < 20) w->y = 20;
    if (w->x + w->w > sw) w->x = sw - w->w;
    if (w->y + w->h > sh) w->y = sh - w->h;
}

static __attribute__((used)) void obwm_main(void) {
    struct ng_fb fb;
    struct ng_theme th;
    int kfd = -1, mfd = -1;
    int mx = 48, my = 48;
    int lmb = 0;
    int shift = 0;
    struct win termw;
    struct win filesw;
    int drag_dx = 0, drag_dy = 0;
    int drag_target = 0;
    int dirty = 1;

    if (ng_fb_open(&fb) < 0) {
        printf("obwm: /dev/fb0 unavailable\n");
        _exit(1);
    }
    th = ng_theme_ctwm();

    kfd = open("/dev/input/event0", O_RDONLY, 0);
    mfd = open("/dev/input/event1", O_RDONLY, 0);
    if (kfd < 0) {
        printf("obwm: input devices unavailable\n");
        ng_fb_close(&fb);
        _exit(2);
    }

    termw.x = 24; termw.y = 36; termw.w = (int)fb.width / 2 + 70; termw.h = (int)fb.height - 56; termw.title = "oterm"; termw.visible = 1; termw.moving = 0;
    filesw.x = termw.x + termw.w + 8; filesw.y = 36; filesw.w = (int)fb.width - filesw.x - 20; filesw.h = (int)fb.height - 56; filesw.title = "ofiles"; filesw.visible = 1; filesw.moving = 0;
    clamp_window_to_screen(&termw, (int)fb.width, (int)fb.height);
    clamp_window_to_screen(&filesw, (int)fb.width, (int)fb.height);

    term_push("obwm started");
    if (mfd < 0) term_push("mouse unavailable: keyboard mode");
    else term_push("mouse ready");
    term_push("type: help");
    load_files();
    redraw(&fb, &th, &termw, &filesw, mx, my);
    dirty = 0;

    for (;;) {
        struct pollfd_compat pfd[2];
        struct ng_input_event ev;
        long pr;
        pfd[0].fd = kfd; pfd[0].events = POLLIN; pfd[0].revents = 0;
        pfd[1].fd = mfd; pfd[1].events = POLLIN; pfd[1].revents = 0;
        pr = syscall3(SYS_POLL, (long)pfd, (mfd >= 0) ? 2 : 1, 40);
        if (pr < 0) continue;

        if (mfd >= 0 && (pfd[1].revents & POLLIN)) {
            while (read(mfd, &ev, sizeof(ev)) == (long)sizeof(ev)) {
                if (ev.type == NG_EV_REL) {
                    int old_mx = mx, old_my = my;
                    if (ev.code == NG_REL_X) mx += ev.value;
                    if (ev.code == NG_REL_Y) my += ev.value;
                    if (mx < 0) mx = 0;
                    if (my < 0) my = 0;
                    if (mx >= (int)fb.width) mx = (int)fb.width - 1;
                    if (my >= (int)fb.height) my = (int)fb.height - 1;
                    if (mx != old_mx || my != old_my) dirty = 1;
                    if (lmb && drag_target == 1) {
                        int old_x = termw.x, old_y = termw.y;
                        termw.x = mx - drag_dx; termw.y = my - drag_dy;
                        clamp_window_to_screen(&termw, (int)fb.width, (int)fb.height);
                        if (termw.x != old_x || termw.y != old_y) dirty = 1;
                    } else if (lmb && drag_target == 2) {
                        int old_x = filesw.x, old_y = filesw.y;
                        filesw.x = mx - drag_dx; filesw.y = my - drag_dy;
                        clamp_window_to_screen(&filesw, (int)fb.width, (int)fb.height);
                        if (filesw.x != old_x || filesw.y != old_y) dirty = 1;
                    }
                } else if (ev.type == NG_EV_KEY && ev.code == NG_BTN_LEFT) {
                    int old_drag = drag_target;
                    lmb = ev.value ? 1 : 0;
                    if (lmb) {
                        if (in_title(&termw, mx, my)) {
                            drag_target = 1; drag_dx = mx - termw.x; drag_dy = my - termw.y;
                        } else if (in_title(&filesw, mx, my)) {
                            drag_target = 2; drag_dx = mx - filesw.x; drag_dy = my - filesw.y;
                        } else {
                            drag_target = 0;
                        }
                    } else {
                        drag_target = 0;
                    }
                    if (drag_target != old_drag) dirty = 1;
                }
            }
        }
        if (pfd[0].revents & POLLIN) {
            while (read(kfd, &ev, sizeof(ev)) == (long)sizeof(ev)) {
                if (ev.type != NG_EV_KEY) continue;
                if (ev.code == NG_KEY_LEFTSHIFT || ev.code == NG_KEY_RIGHTSHIFT) {
                    shift = (ev.value != 0);
                    continue;
                }
                if (ev.value != 1) continue;
                if (ev.code == NG_KEY_ESC) {
                    close(kfd);
                    if (mfd >= 0) close(mfd);
                    ng_fb_close(&fb);
                    _exit(0);
                } else if (ev.code == NG_KEY_ENTER) {
                    do_term_cmd();
                    dirty = 1;
                } else if (ev.code == NG_KEY_BACKSPACE) {
                    if (g_term_len > 0) {
                        g_term_line[--g_term_len] = 0;
                        dirty = 1;
                    }
                } else {
                    char ch = ng_keycode_to_ascii(ev.code, shift);
                    if (ch && g_term_len < (int)sizeof(g_term_line) - 1) {
                        g_term_line[g_term_len++] = ch;
                        g_term_line[g_term_len] = 0;
                        dirty = 1;
                    }
                }
            }
        }
        if (dirty) {
            redraw(&fb, &th, &termw, &filesw, mx, my);
            dirty = 0;
        }
    }
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "andq $-16, %rsp\n"
        "call obwm_main\n"
        "mov $0, %edi\n"
        "call _exit\n");
}
