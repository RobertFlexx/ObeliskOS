/*
 * Obelisk — df(1): filesystem usage via statfs(2).
 */

#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int statfs(const char *pathname, void *buf);

/* Must match kernel struct statfs_compat */
struct statfs_compat {
    long long f_type;
    long long f_bsize;
    unsigned long long f_blocks;
    unsigned long long f_bfree;
    unsigned long long f_bavail;
    unsigned long long f_files;
    unsigned long long f_ffree;
    int f_fsid[2];
    long long f_namelen;
    long long f_frsize;
    long long f_flags;
    long long f_spare[4];
} __attribute__((packed));

static void wrout(const char *s) {
    (void)write(1, s, strlen(s));
}

static void wr_u64(unsigned long long v) {
    char tmp[24];
    int i = 0;
    if (v == 0) {
        wrout("0");
        return;
    }
    while (v > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10ULL));
        v /= 10ULL;
    }
    while (i > 0) {
        char c[2] = { tmp[--i], '\0' };
        wrout(c);
    }
}

static void show_one(const char *path) {
    struct statfs_compat st;

    memset(&st, 0, sizeof(st));
    if (statfs(path, &st) < 0) {
        wrout("df: ");
        wrout(path);
        wrout(": statfs failed\n");
        return;
    }
    wrout(path);
    wrout("\tType:");
    wr_u64((unsigned long long)st.f_type);
    wrout("\tBlockSize:");
    wr_u64((unsigned long long)(st.f_bsize > 0 ? (unsigned long long)st.f_bsize : 0ULL));
    wrout("\tBlocks:");
    wr_u64(st.f_blocks);
    wrout("\tFree:");
    wr_u64(st.f_bfree);
    wrout("\n");
}

int main(int argc, char **argv) {
    int i;

    if (argc < 2) {
        show_one("/");
        return 0;
    }
    for (i = 1; i < argc; i++) {
        show_one(argv[i]);
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
