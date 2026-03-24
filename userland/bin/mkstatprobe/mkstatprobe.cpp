/*
 * Obelisk OS - mkstatprobe (C++)
 * From Axioms, Order.
 */

#include <stdint.h>

static constexpr long SYS_WRITE = 1;
static constexpr long SYS_OPEN  = 2;
static constexpr long SYS_CLOSE = 3;
static constexpr long SYS_STAT  = 4;
static constexpr long SYS_MKDIR = 83;
static constexpr long SYS_EXIT  = 60;

static constexpr long O_CREAT  = 0100;
static constexpr long O_WRONLY = 01;
static constexpr long O_TRUNC  = 01000;

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

static unsigned long str_len(const char *s) { unsigned long n = 0; while (s && s[n]) n++; return n; }
static void put_str(const char *s) { call3(SYS_WRITE, 1, (long)s, (long)str_len(s)); }
static void put_u(unsigned long v) {
    char tmp[32]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    else { char rev[32]; int r = 0; while (v > 0 && r < 32) { rev[r++] = (char)('0' + (v % 10)); v /= 10; } while (r > 0) tmp[n++] = rev[--r]; }
    tmp[n] = '\0'; put_str(tmp);
}

static void print_stat(const char *tag, const char *path) {
    stat_compat st {};
    long r = call2(SYS_STAT, (long)path, (long)&st);
    put_str(tag); put_str(" stat_ret="); put_u((unsigned long)(r < 0 ? -r : r));
    put_str(" uid="); put_u(st.st_uid);
    put_str(" gid="); put_u(st.st_gid);
    put_str(" mode="); put_u((unsigned long)(st.st_mode & 07777));
    put_str("\n");
}

extern "C" void _start() {
    static const char *f = "/tmp/mkstatprobe.file";
    static const char *d = "/tmp/mkstatprobe.dir";
    long fd = call3(SYS_OPEN, (long)f, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) call1(SYS_CLOSE, fd);
    call2(SYS_MKDIR, (long)d, 0755);
    print_stat("file", f);
    print_stat("dir", d);
    call1(SYS_EXIT, 0);
    __builtin_unreachable();
}
