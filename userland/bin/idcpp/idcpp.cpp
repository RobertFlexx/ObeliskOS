/*
 * Obelisk OS - idcpp (C++)
 * From Axioms, Order.
 */

#include <stdint.h>

static constexpr long SYS_WRITE = 1;
static constexpr long SYS_EXIT = 60;
static constexpr long SYS_GETUID = 102;
static constexpr long SYS_GETGID = 104;
static constexpr long SYS_GETEUID = 107;
static constexpr long SYS_GETEGID = 108;

static long call0(long num) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "cc", "memory"
    );
    return ret;
}

static long call1(long num, long a1) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "cc", "memory"
    );
    return ret;
}

static long call3(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "cc", "memory"
    );
    return ret;
}

static unsigned long str_len(const char *s) {
    unsigned long n = 0;
    while (s && s[n]) n++;
    return n;
}

static void put_str(const char *s) {
    call3(SYS_WRITE, 1, (long)s, (long)str_len(s));
}

static void put_u(long v) {
    char tmp[32];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        char rev[32];
        int r = 0;
        while (v > 0 && r < (int)sizeof(rev)) {
            rev[r++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (r > 0) tmp[n++] = rev[--r];
    }
    tmp[n] = '\0';
    put_str(tmp);
}

extern "C" void _start(void) {
    long uid = call0(SYS_GETUID);
    long gid = call0(SYS_GETGID);
    long euid = call0(SYS_GETEUID);
    long egid = call0(SYS_GETEGID);

    put_str("uid=");
    put_u(uid);
    put_str(" gid=");
    put_u(gid);
    put_str(" euid=");
    put_u(euid);
    put_str(" egid=");
    put_u(egid);
    put_str("\n");

    call1(SYS_EXIT, 0);
    __builtin_unreachable();
}
