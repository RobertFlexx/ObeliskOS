/*
 * Obelisk OS - credprobe (C++)
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

static void put_i(long v) {
    if (v < 0) {
        put_str("-");
        v = -v;
    }
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

static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static long parse_i(const char *s, int *ok) {
    long v = 0;
    int i = 0;
    int sign = 1;
    *ok = 0;
    if (!s || !s[0]) return 0;
    if (s[0] == '-') {
        sign = -1;
        i++;
    }
    if (s[i] < '0' || s[i] > '9') return 0;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (long)(s[i] - '0');
        i++;
    }
    if (s[i] != '\0') return 0;
    *ok = 1;
    return v * sign;
}

static void print_state() {
    put_str("uid=");
    put_i(call0(SYS_GETUID));
    put_str(" gid=");
    put_i(call0(SYS_GETGID));
    put_str(" euid=");
    put_i(call0(SYS_GETEUID));
    put_str(" egid=");
    put_i(call0(SYS_GETEGID));
    put_str("\n");
}

extern "C" void _start() {
    uint64_t *stack_ptr;
    __asm__ volatile("movq %%rsp, %0" : "=r"(stack_ptr));

    int argc = (int)stack_ptr[0];
    char **argv = (char **)&stack_ptr[1];

    if (argc == 1) {
        print_state();
        call1(SYS_EXIT, 0);
        __builtin_unreachable();
    }

    if ((argc - 1) % 2 != 0) {
        put_str("usage: credprobe [setuid <id>|setgid <id>]...\n");
        call1(SYS_EXIT, 1);
        __builtin_unreachable();
    }

    for (int i = 1; i + 1 < argc; i += 2) {
        int ok = 0;
        long id = parse_i(argv[i + 1], &ok);
        long ret;
        if (!ok) {
            put_str("credprobe: bad id\n");
            call1(SYS_EXIT, 2);
            __builtin_unreachable();
        }
        if (str_cmp(argv[i], "setuid") == 0) {
            ret = call1(SYS_SETUID, id);
            put_str("setuid(");
            put_i(id);
            put_str(") ret=");
            put_i(ret);
            put_str("\n");
            continue;
        }
        if (str_cmp(argv[i], "setgid") == 0) {
            ret = call1(SYS_SETGID, id);
            put_str("setgid(");
            put_i(id);
            put_str(") ret=");
            put_i(ret);
            put_str("\n");
            continue;
        }
        put_str("usage: credprobe [setuid <id>|setgid <id>]...\n");
        call1(SYS_EXIT, 1);
        __builtin_unreachable();
    }
    print_state();
    call1(SYS_EXIT, 0);
    __builtin_unreachable();
}
