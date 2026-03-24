/*
 * Obelisk OS - execprobe (C++)
 * From Axioms, Order.
 */

#include <stdint.h>

static constexpr long SYS_WRITE = 1;
static constexpr long SYS_EXECVE = 59;
static constexpr long SYS_EXIT = 60;

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

extern "C" void _start() {
    uint64_t *stack_ptr;
    __asm__ volatile("movq %%rsp, %0" : "=r"(stack_ptr));
    int argc = (int)stack_ptr[0];
    char **argv = (char **)&stack_ptr[1];

    if (argc < 2) {
        put_str("usage: execprobe <path>\n");
        call1(SYS_EXIT, 1);
        __builtin_unreachable();
    }

    char *exec_argv[2];
    char *exec_envp[2];
    exec_argv[0] = argv[1];
    exec_argv[1] = nullptr;
    exec_envp[0] = (char *)"PATH=/bin:/sbin:/usr/bin";
    exec_envp[1] = nullptr;

    long ret = call3(SYS_EXECVE, (long)argv[1], (long)exec_argv, (long)exec_envp);
    put_str("execve(");
    put_str(argv[1]);
    put_str(") ret=");
    put_i(ret);
    put_str("\n");
    call1(SYS_EXIT, 0);
    __builtin_unreachable();
}
