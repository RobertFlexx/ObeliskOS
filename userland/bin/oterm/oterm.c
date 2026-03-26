#include <stddef.h>
#include <stdint.h>

#include "../../lib/native_gui.h"

extern int printf(const char *fmt, ...);
extern int open(const char *pathname, int flags, int mode);
extern long read(int fd, void *buf, unsigned long count);
extern int close(int fd);
extern void _exit(int status);
extern void *memset(void *s, int c, size_t n);
extern int strcmp(const char *a, const char *b);
extern int fork(void);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int waitpid(int pid, int *status, int options);
extern long write(int fd, const void *buf, unsigned long count);
extern int unlink(const char *path);

#define O_RDONLY 0x0000
#define SYS_POLL 7

struct pollfd_compat {
    int fd;
    short events;
    short revents;
} __attribute__((packed));

#define POLLIN 0x0001

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

static char g_lines[40][100];
static int g_rows;
static char g_cmd[100];
static int g_cmd_len;

static void push(const char *s) {
    int i = (g_rows < 40) ? g_rows++ : 39;
    while (i > 0) {
        int j = 0;
        while (g_lines[i - 1][j]) { g_lines[i][j] = g_lines[i - 1][j]; j++; }
        g_lines[i][j] = 0;
        i--;
    }
    i = 0;
    while (s[i] && i < 99) { g_lines[0][i] = s[i]; i++; }
    g_lines[0][i] = 0;
}

static void run_cmd(void) {
    int pid;
    int st = 0;
    char cmdline[180];
    static const char out_path[] = "/tmp/oterm.out";
    if (g_cmd[0] == 0) {
        push("");
        g_cmd[0] = 0;
        g_cmd_len = 0;
        return;
    }
    if (strcmp(g_cmd, "help") == 0) {
        push("oterm: runs system commands via /bin/osh -c");
        push("builtins: help, clear");
        g_cmd[0] = 0;
        g_cmd_len = 0;
        return;
    }
    if (strcmp(g_cmd, "clear") == 0) {
        g_rows = 0;
        g_cmd[0] = 0;
        g_cmd_len = 0;
        return;
    }

    {
        int i = 0;
        int p = 0;
        const char tail[] = " >/tmp/oterm.out 2>&1";
        while (g_cmd[i] && p < (int)sizeof(cmdline) - 1) cmdline[p++] = g_cmd[i++];
        i = 0;
        while (tail[i] && p < (int)sizeof(cmdline) - 1) cmdline[p++] = tail[i++];
        cmdline[p] = 0;
    }
    unlink(out_path);
    pid = fork();
    if (pid == 0) {
        char *args[] = { "osh", "-c", cmdline, 0 };
        char *envp[] = {
            "PATH=/bin:/sbin:/usr/bin",
            "HOME=/home/obelisk",
            "TERM=vt100",
            "USER=obelisk",
            "SHELL=/bin/osh",
            0
        };
        execve("/bin/osh", args, envp);
        _exit(127);
    }
    if (pid < 0) {
        push("oterm: fork failed");
    } else {
        char buf[256];
        int fd;
        int used = 0;
        while (waitpid(pid, &st, 0) < 0) { }
        fd = open(out_path, O_RDONLY, 0);
        if (fd >= 0) {
            for (;;) {
                long n = read(fd, buf, sizeof(buf));
                int i;
                if (n <= 0) break;
                for (i = 0; i < (int)n; i++) {
                    if (buf[i] == '\r') continue;
                    if (buf[i] == '\n') {
                        cmdline[used] = 0;
                        push(cmdline);
                        used = 0;
                    } else if (used < (int)sizeof(cmdline) - 1) {
                        cmdline[used++] = buf[i];
                    }
                }
            }
            if (used > 0) {
                cmdline[used] = 0;
                push(cmdline);
            }
            close(fd);
            unlink(out_path);
        }
        if (st != 0) {
            push("oterm: command failed");
        }
    }
    g_cmd[0] = 0;
    g_cmd_len = 0;
}

static __attribute__((used)) void oterm_main(void) {
    struct ng_fb fb;
    struct ng_theme th;
    int kfd;
    int shift = 0;
    int i;
    int dirty = 1;
    if (ng_fb_open(&fb) < 0) {
        printf("oterm: no /dev/fb0\n");
        _exit(1);
    }
    kfd = open("/dev/input/event0", O_RDONLY, 0);
    if (kfd < 0) {
        ng_fb_close(&fb);
        _exit(2);
    }
    th = ng_theme_ctwm();
    push("oterm ready");
    push("input: /dev/input/event0");
    for (;;) {
        struct pollfd_compat p;
        struct ng_input_event ev;
        p.fd = kfd; p.events = POLLIN; p.revents = 0;
        syscall3(SYS_POLL, (long)&p, 1, 30);
        if (p.revents & POLLIN) {
            while (read(kfd, &ev, sizeof(ev)) == (long)sizeof(ev)) {
                if (ev.type != NG_EV_KEY) continue;
                if (ev.code == NG_KEY_LEFTSHIFT || ev.code == NG_KEY_RIGHTSHIFT) {
                    shift = ev.value ? 1 : 0; continue;
                }
                if (ev.value != 1) continue;
                if (ev.code == NG_KEY_ESC) { close(kfd); ng_fb_close(&fb); _exit(0); }
                if (ev.code == NG_KEY_ENTER) { run_cmd(); dirty = 1; }
                else if (ev.code == NG_KEY_BACKSPACE) { if (g_cmd_len > 0) { g_cmd[--g_cmd_len] = 0; dirty = 1; } }
                else {
                    char c = ng_keycode_to_ascii(ev.code, shift);
                    if (c && g_cmd_len < 99) { g_cmd[g_cmd_len++] = c; g_cmd[g_cmd_len] = 0; dirty = 1; }
                }
            }
        }
        if (dirty) {
            ng_fill(&fb, th.desktop_bg);
            ng_rect(&fb, 8, 8, (int)fb.width - 16, (int)fb.height - 16, 0x121821);
            ng_frame_3d(&fb, 8, 8, (int)fb.width - 16, (int)fb.height - 16, th.win_border_light, th.win_border_dark);
            ng_text(&fb, 14, 14, "oterm (ESC to exit)", th.text_fg, 0x121821);
            for (i = 0; i < g_rows && i < 28; i++) ng_text(&fb, 14, 30 + i * 10, g_lines[i], th.text_fg, 0x121821);
            ng_text(&fb, 14, (int)fb.height - 24, "osh$ ", th.text_fg, 0x121821);
            ng_text(&fb, 54, (int)fb.height - 24, g_cmd, th.text_fg, 0x121821);
            dirty = 0;
        }
    }
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "andq $-16, %rsp\n"
        "call oterm_main\n"
        "mov $0, %edi\n"
        "call _exit\n");
}
