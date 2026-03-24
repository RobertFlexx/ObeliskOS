/*
 * Obelisk OS - setuidcheck (C++)
 * From Axioms, Order.
 */

#include <stdint.h>

static constexpr long SYS_WRITE = 1;
static constexpr long SYS_EXIT = 60;
static constexpr long SYS_GETUID = 102;
static constexpr long SYS_GETGID = 104;
static constexpr long SYS_SETUID = 105;
static constexpr long SYS_SETGID = 106;
static constexpr long SYS_GETEUID = 107;
static constexpr long SYS_GETEGID = 108;

static long call0(long num) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "cc", "memory");
    return ret;
}

static long call1(long num, long a1) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "D"(a1) : "cc", "memory");
    return ret;
}

static long call3(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "cc", "memory");
    return ret;
}

static unsigned long str_len(const char *s) {
    unsigned long n = 0;
    while (s && s[n]) n++;
    return n;
}

static void put_str(const char *s) { call3(SYS_WRITE, 1, (long)s, (long)str_len(s)); }

static void put_i(long v) {
    if (v < 0) { put_str("-"); v = -v; }
    char tmp[32];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    else {
        char rev[32];
        int r = 0;
        while (v > 0 && r < (int)sizeof(rev)) { rev[r++] = (char)('0' + (v % 10)); v /= 10; }
        while (r > 0) tmp[n++] = rev[--r];
    }
    tmp[n] = '\0';
    put_str(tmp);
}

static void print_state() {
    put_str("uid="); put_i(call0(SYS_GETUID));
    put_str(" gid="); put_i(call0(SYS_GETGID));
    put_str(" euid="); put_i(call0(SYS_GETEUID));
    put_str(" egid="); put_i(call0(SYS_GETEGID));
    put_str("\n");
}

extern "C" void _start(void) {
    print_state();
    long r1 = call1(SYS_SETUID, 1000);
    put_str("setuid(1000) ret="); put_i(r1); put_str("\n");
    print_state();
    long r2 = call1(SYS_SETUID, 0);
    put_str("setuid(0) ret="); put_i(r2); put_str("\n");
    print_state();
    long g1 = call1(SYS_SETGID, 1000);
    put_str("setgid(1000) ret="); put_i(g1); put_str("\n");
    print_state();
    long g2 = call1(SYS_SETGID, 0);
    put_str("setgid(0) ret="); put_i(g2); put_str("\n");
    print_state();
    call1(SYS_EXIT, 0);
    __builtin_unreachable();
}
