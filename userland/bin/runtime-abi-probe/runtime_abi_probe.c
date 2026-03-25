/*
 * Obelisk runtime ABI probe for desktop prerequisites.
 */

#include <stddef.h>
#include <stdint.h>

extern int printf(const char *fmt, ...);
extern long read(int fd, void *buf, unsigned long count);
extern long write(int fd, const void *buf, unsigned long count);
extern int close(int fd);

#define SYS_POLL 7
#define SYS_SELECT 23
#define SYS_SOCKETPAIR 53
#define SYS_SETPGID 109
#define SYS_GETPGRP 111
#define SYS_SETSID 112
#define SYS_GETPGID 121
#define SYS_GETSID 124

#define AF_UNIX 1
#define SOCK_STREAM 1

#define POLLIN 0x0001
#define POLLOUT 0x0004

struct pollfd_compat {
    int fd;
    int16_t events;
    int16_t revents;
} __attribute__((packed));

struct timeval_compat {
    int64_t tv_sec;
    int64_t tv_usec;
};

static inline long ob_syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall4(long n, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall1(long n, long a1) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "cc", "memory"
    );
    return ret;
}

static int fdset_test(uint64_t *set, int fd) {
    return (set[fd / 64] & (1ULL << (fd % 64))) != 0;
}

static void fdset_set(uint64_t *set, int fd) {
    set[fd / 64] |= (1ULL << (fd % 64));
}

void _start(void) {
    int sv[2] = {-1, -1};
    char c = 'X';
    char out = 0;
    int failed = 0;

    printf("runtime-abi-probe: start\n");

    long rc = ob_syscall4(SYS_SOCKETPAIR, AF_UNIX, SOCK_STREAM, 0, (long)sv);
    if (rc < 0) {
        printf("[fail] socketpair(AF_UNIX,SOCK_STREAM): %ld\n", rc);
        failed = 1;
        goto done;
    }
    printf("[ok] socketpair fds=%d,%d\n", sv[0], sv[1]);

    if (write(sv[0], &c, 1) != 1) {
        printf("[fail] write(socketpair)\n");
        failed = 1;
        goto done;
    }

    struct pollfd_compat pfd;
    pfd.fd = sv[1];
    pfd.events = POLLIN | POLLOUT;
    pfd.revents = 0;
    rc = ob_syscall3(SYS_POLL, (long)&pfd, 1, 200);
    if (rc < 0 || !(pfd.revents & POLLIN)) {
        printf("[fail] poll(POLLIN): rc=%ld revents=0x%x\n", rc, (unsigned)pfd.revents);
        failed = 1;
        goto done;
    }
    printf("[ok] poll detected readable socketpair data\n");

    if (read(sv[1], &out, 1) != 1 || out != 'X') {
        printf("[fail] read(socketpair)\n");
        failed = 1;
        goto done;
    }
    printf("[ok] socketpair data path\n");

    {
        uint64_t rfds[16];
        uint64_t wfds[16];
        struct timeval_compat tv;
        int nfds = sv[1] + 1;
        for (int i = 0; i < 16; i++) {
            rfds[i] = 0;
            wfds[i] = 0;
        }
        fdset_set(wfds, sv[1]);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        rc = ob_syscall5(SYS_SELECT, nfds, (long)rfds, (long)wfds, 0, (long)&tv);
        if (rc < 0 || !fdset_test(wfds, sv[1])) {
            printf("[fail] select(writefds): rc=%ld\n", rc);
            failed = 1;
            goto done;
        }
        printf("[ok] select writefds path\n");
    }

    rc = ob_syscall2(SYS_SETPGID, 0, 0);
    if (rc < 0) {
        printf("[fail] setpgid(0,0): %ld\n", rc);
        failed = 1;
        goto done;
    }
    rc = ob_syscall1(SYS_GETPGRP, 0);
    if (rc < 0) {
        printf("[fail] getpgrp: %ld\n", rc);
        failed = 1;
        goto done;
    }
    printf("[ok] process-group syscalls pgrp=%ld\n", rc);

    rc = ob_syscall1(SYS_GETSID, 0);
    if (rc < 0) {
        printf("[fail] getsid(0): %ld\n", rc);
        failed = 1;
        goto done;
    }
    printf("[ok] getsid(0)=%ld\n", rc);

done:
    if (sv[0] >= 0) close(sv[0]);
    if (sv[1] >= 0) close(sv[1]);
    if (failed) {
        printf("runtime-abi-probe: FAIL\n");
        __asm__ volatile("int $0x80" : : "a"(60), "D"(1) : "cc", "memory");
    } else {
        printf("runtime-abi-probe: PASS\n");
        __asm__ volatile("int $0x80" : : "a"(60), "D"(0) : "cc", "memory");
    }
    __builtin_unreachable();
}
