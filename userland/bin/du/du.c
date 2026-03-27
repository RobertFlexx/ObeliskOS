/*
 * Obelisk — du(1): sum of st_size for all regular files under each path (recursive).
 */

#include <stdint.h>
#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int stat(const char *pathname, void *statbuf);
extern int open(const char *pathname, int flags, int mode);
extern int close(int fd);
extern ssize_t getdents64(int fd, void *dirp, size_t count);
extern void *memcpy(void *d, const void *s, size_t n);

#define O_RDONLY 0
#define O_DIRECTORY 0x10000
#define S_IFMT 0170000u
#define S_IFDIR 0040000u

struct ob_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    int64_t st_atime_nsec;
    int64_t st_mtime;
    int64_t st_mtime_nsec;
    int64_t st_ctime;
    int64_t st_ctime_nsec;
    int64_t unused_pad[3];
};

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
} __attribute__((packed));

static void wr_u64(unsigned long long v) {
    char tmp[24];
    int i = 0;

    if (v == 0) {
        (void)write(1, "0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10ULL));
        v /= 10ULL;
    }
    while (i > 0) {
        char c[2] = { tmp[--i], '\0' };
        (void)write(1, c, 1);
    }
}

static int64_t du_sum(const char *path, int depth) {
    struct ob_stat st;
    char dbuf[2048];
    long nread;
    long bpos;
    int fd;
    int64_t sum = 0;

    if (depth > 64) {
        return -1;
    }
    if (stat(path, &st) < 0) {
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        return st.st_size > 0 ? st.st_size : 0;
    }
    fd = open(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return -1;
    }
    while (1) {
        nread = (long)getdents64(fd, dbuf, sizeof(dbuf));
        if (nread < 0) {
            close(fd);
            return -1;
        }
        if (nread == 0) {
            break;
        }
        bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(dbuf + bpos);
            const char *nm = d->d_name;
            char child[512];
            size_t pl = strlen(path);
            size_t nl = strlen(nm);
            int64_t sub;

            if (nm[0] == '.' &&
                (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) {
                goto next;
            }
            if (pl + 1 + nl + 1 >= sizeof(child)) {
                goto next;
            }
            memcpy(child, path, pl);
            if (pl > 0 && child[pl - 1] != '/') {
                child[pl++] = '/';
            }
            memcpy(child + pl, nm, nl + 1);
            sub = du_sum(child, depth + 1);
            if (sub < 0) {
                close(fd);
                return -1;
            }
            sum += sub;
        next:
            if (d->d_reclen == 0) {
                break;
            }
            bpos += d->d_reclen;
        }
    }
    close(fd);
    return sum;
}

static void show(const char *path) {
    int64_t n = du_sum(path, 0);

    if (n < 0) {
        (void)write(2, "du: error\n", 10);
        return;
    }
    wr_u64((unsigned long long)n);
    (void)write(1, "\t", 1);
    (void)write(1, path, strlen(path));
    (void)write(1, "\n", 1);
}

int main(int argc, char **argv) {
    int i;

    if (argc < 2) {
        show(".");
        return 0;
    }
    for (i = 1; i < argc; i++) {
        show(argv[i]);
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
