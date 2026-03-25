/*
 * Obelisk OS - Init Process
 * From Axioms, Order.
 *
 * The first userspace process (PID 1).
 */

#include <stdint.h>
#include <stddef.h>

/* System call numbers */
#define SYS_EXIT        60
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_EXECVE      59
#define SYS_GETPID      39
#define SYS_REBOOT      169
#define SYS_SETUID      105
#define SYS_SETGID      106

/* Inline syscall wrappers */
static inline long syscall0(long num) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "cc", "memory"
    );
    return ret;
}

static inline long syscall1(long num, long arg1) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "cc", "memory"
    );
    return ret;
}

static inline long syscall3(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "cc", "memory"
    );
    return ret;
}

static inline long syscall4(long num, long arg1, long arg2, long arg3, long arg4) {
    long ret;
    register long r10 __asm__("r10") = arg4;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "cc", "memory"
    );
    return ret;
}

static int reboot_cmd(unsigned int cmd) {
    return (int)syscall4(SYS_REBOOT, 0xfee1dead, 0x28121969, cmd, 0);
}

/* Simple strlen */
static size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

/* Write to stdout */
static void print(const char *s) {
    syscall3(SYS_WRITE, 1, (long)s, strlen(s));
}

/* Exit process */
static void exit(int status) {
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

/* Minimal strcmp */
static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* getpid */
static int getpid(void) {
    return (int)syscall0(SYS_GETPID);
}

/* execve */
static int execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
}

static int setuid(unsigned uid) {
    return (int)syscall1(SYS_SETUID, (long)uid);
}

static int setgid(unsigned gid) {
    return (int)syscall1(SYS_SETGID, (long)gid);
}

/* read */
static long read(int fd, void *buf, unsigned long count) {
    return syscall3(SYS_READ, fd, (long)buf, count);
}

/* interactive fallback shell */
static void fallback_shell(void) {
    char line[256];
    print("Starting fallback init shell (built-in)...\n");
    print("Commands: help, clear, uname, reboot, shutdown, exit\n");

    while (1) {
        print("init# ");
        long n = read(0, line, sizeof(line) - 1);
        if (n <= 0) {
            print("\nread error\n");
            continue;
        }
        line[n] = '\0';
        for (long i = 0; i < n; i++) {
            if (line[i] == '\n' || line[i] == '\r') {
                line[i] = '\0';
                break;
            }
        }

        if (strcmp(line, "help") == 0) {
            print("help, clear, uname, reboot, shutdown, exit\n");
        } else if (strcmp(line, "clear") == 0) {
            print("\033[3J\033[2J\033[H");
        } else if (strcmp(line, "uname") == 0) {
            print("Obelisk OS\n");
        } else if (strcmp(line, "reboot") == 0) {
            (void)reboot_cmd(0x01234567);
            print("Reboot syscall failed.\n");
        } else if (strcmp(line, "shutdown") == 0 ||
                   strcmp(line, "poweroff") == 0 ||
                   strcmp(line, "halt") == 0) {
            (void)reboot_cmd(0x4321FEDC);
            print("Shutdown syscall failed.\n");
        } else if (strcmp(line, "exit") == 0) {
            print("Staying alive as PID 1.\n");
        } else if (line[0] != '\0') {
            print("unknown command\n");
        }
    }
}

/* Entry point */
void _start(void) {
    print("\n");
    print("========================================\n");
    print("  Obelisk OS - Init Process\n");
    print("  From Axioms, Order.\n");
    print("========================================\n");
    print("\n");

    int pid = getpid();
    print("Init started (PID ");
    char pid_buf[16];
    int p = pid;
    int idx = 0;
    if (p <= 0) {
        pid_buf[idx++] = '0';
    } else {
        char tmp[16];
        int t = 0;
        while (p > 0 && t < 15) {
            tmp[t++] = (char)('0' + (p % 10));
            p /= 10;
        }
        while (t > 0) {
            pid_buf[idx++] = tmp[--t];
        }
    }
    pid_buf[idx] = '\0';
    print(pid_buf);
    print(")\n");
    print("Boot target: /bin/osh (Obelisk shell)\n");

    char *const envp[] = {
        "PATH=/bin:/sbin:/usr/bin",
        "HOME=/home/obelisk",
        "SHELL=/bin/osh",
        "USER=obelisk",
        "TERM=vt100",
        NULL
    };
    char *const sh_argv[] = { "sh", NULL };
    char *const osh_argv[] = { "osh", "-i", NULL };

    print("Launching Obelisk shell...\n");
    print("Dropping to default user: obelisk (uid/gid 1000)\n");
    if (setgid(1000) < 0 || setuid(1000) < 0) {
        print("init: warning: failed to drop privileges, continuing as root\n");
    }
    if (execve("/bin/osh", osh_argv, envp) < 0) {
        print("init: /bin/osh failed, trying /bin/sh\n");
        if (execve("/bin/sh", sh_argv, envp) < 0) {
            print("init: all shell exec attempts failed\n");
            fallback_shell();
        }
    }

    exit(1);
}
