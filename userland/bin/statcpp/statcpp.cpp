/*
 * Obelisk OS - statcpp (C++)
 * From Axioms, Order.
 */

#include <stdint.h>

static constexpr long SYS_WRITE = 1;
static constexpr long SYS_STAT = 4;
static constexpr long SYS_EXIT = 60;

struct stat_compat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t  __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_atime_nsec;
    int64_t  st_mtime;
    int64_t  st_mtime_nsec;
    int64_t  st_ctime;
    int64_t  st_ctime_nsec;
    int64_t  unused_pad[3];
};

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

static long call2(long num, long a1, long a2) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
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

static void put_u(uint64_t v) {
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

static void put_o(uint64_t v) {
    char tmp[32];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        char rev[32];
        int r = 0;
        while (v > 0 && r < (int)sizeof(rev)) {
            rev[r++] = (char)('0' + (v & 0x7));
            v >>= 3;
        }
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
        put_str("usage: statcpp <path>\n");
        call1(SYS_EXIT, 1);
        __builtin_unreachable();
    }

    stat_compat st {};
    long ret = call2(SYS_STAT, (long)argv[1], (long)&st);
    if (ret < 0) {
        put_str("statcpp: stat failed\n");
        call1(SYS_EXIT, 2);
        __builtin_unreachable();
    }

    put_str("path=");
    put_str(argv[1]);
    put_str(" uid=");
    put_u(st.st_uid);
    put_str(" gid=");
    put_u(st.st_gid);
    put_str(" mode=0");
    put_o(st.st_mode & 07777);
    put_str(" type=0");
    put_o(st.st_mode & 0170000);
    put_str("\n");

    call1(SYS_EXIT, 0);
    __builtin_unreachable();
}
