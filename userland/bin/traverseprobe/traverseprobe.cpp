/*
 * Obelisk OS - traverseprobe (C++)
 * From Axioms, Order.
 */

#include <stdint.h>

static constexpr long SYS_WRITE  = 1;
static constexpr long SYS_OPEN   = 2;
static constexpr long SYS_CLOSE  = 3;
static constexpr long SYS_MKDIR  = 83;
static constexpr long SYS_SETUID = 105;
static constexpr long SYS_SETGID = 106;
static constexpr long SYS_EXECVE = 59;
static constexpr long SYS_EXIT   = 60;

static constexpr long O_CREAT  = 0100;
static constexpr long O_WRONLY = 01;
static constexpr long O_TRUNC  = 01000;

static long call1(long num, long a1) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "D"(a1) : "cc", "memory");
    return ret;
}

static long call2(long num, long a1, long a2) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2) : "cc", "memory");
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

extern "C" void _start() {
    static const char *dir = "/tmp/traverseprobe";
    static const char *file = "/tmp/traverseprobe/nonexec";
    long ret;

    ret = call2(SYS_MKDIR, (long)dir, 0700);
    if (ret < 0) {
        put_str("mkdir ret="); put_i(ret); put_str("\n");
    }

    ret = call3(SYS_OPEN, (long)file, O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (ret >= 0) {
        call1(SYS_CLOSE, ret);
    } else {
        put_str("open ret="); put_i(ret); put_str("\n");
    }

    call1(SYS_SETGID, 1000);
    call1(SYS_SETUID, 1000);

    char *argv[2];
    char *envp[2];
    argv[0] = (char *)file;
    argv[1] = nullptr;
    envp[0] = (char *)"PATH=/bin:/sbin:/usr/bin";
    envp[1] = nullptr;
    ret = call3(SYS_EXECVE, (long)file, (long)argv, (long)envp);
    put_str("execve("); put_str(file); put_str(") ret="); put_i(ret); put_str("\n");
    call1(SYS_EXIT, 0);
    __builtin_unreachable();
}
