/*
 * Obelisk — kill(1): send signal to process(es).
 * Usage: kill [-s signum | -signum] pid [pid...]
 */

#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int kill(int pid, int sig);
extern int getpid(void);

static void wrerr(const char *s) {
    (void)write(2, s, strlen(s));
}

static void wrnum(int v) {
    char buf[16];
    int i = 0;
    unsigned x = (v < 0) ? (unsigned)(-v) : (unsigned)v;
    if (v < 0) {
        (void)write(2, "-", 1);
    }
    if (x == 0) {
        (void)write(2, "0", 1);
        return;
    }
    while (x > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (x % 10U));
        x /= 10U;
    }
    while (i > 0) {
        (void)write(2, &buf[--i], 1);
    }
}

static const char *err_text(int e) {
    if (e < 0) e = -e;
    switch (e) {
        case 1: return "operation not permitted";
        case 2: return "no such process";
        case 3: return "no such process";
        case 22: return "invalid argument";
        default: return "failed";
    }
}

static int parse_int(const char *s, int *out) {
    int v = 0;
    int neg = 0;

    if (!s || !s[0]) {
        return -1;
    }
    if (s[0] == '-') {
        neg = 1;
        s++;
    }
    if (!s[0]) {
        return -1;
    }
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        v = v * 10 + (*s - '0');
        if (v > 100000000) {
            return -1;
        }
    }
    *out = neg ? -v : v;
    return 0;
}

int main(int argc, char **argv) {
    int sig = 15;
    int first = 1;
    int self = getpid();

    if (argc < 2) {
        wrerr("usage: kill [-s n | -n] pid ...\n");
        return 1;
    }
    if (argv[1][0] == '-' && argv[1][1] != '\0') {
        if (argv[1][1] == 's' && argv[1][2] == '\0') {
            if (argc < 4) {
                wrerr("kill: -s requires signal and pid\n");
                return 1;
            }
            if (parse_int(argv[2], &sig) < 0) {
                wrerr("kill: bad signal\n");
                return 1;
            }
            first = 3;
        } else {
            if (parse_int(argv[1] + 1, &sig) < 0) {
                wrerr("kill: bad signal\n");
                return 1;
            }
            first = 2;
        }
    }
    if (first >= argc) {
        wrerr("kill: missing pid\n");
        return 1;
    }
    for (; first < argc; first++) {
        int pid;
        int r;

        if (parse_int(argv[first], &pid) < 0) {
            wrerr("kill: invalid pid\n");
            return 1;
        }
        if (pid <= 1) {
            wrerr("kill: refusing dangerous pid ");
            wrnum(pid);
            wrerr("\n");
            return 1;
        }
        if (pid == self) {
            wrerr("kill: refusing to signal current shell process\n");
            return 1;
        }
        r = kill(pid, sig);
        if (r < 0) {
            wrerr("kill: ");
            wrerr(err_text(r));
            wrerr(" (");
            wrnum(-r);
            wrerr(")\n");
            return 1;
        }
    }
    return 0;
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "andq $-16, %rsp\n"
        "call main\n"
        "mov %eax, %edi\n"
        "call _exit\n");
}
