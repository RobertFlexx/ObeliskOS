/*
 * Obelisk — helpers for account tools.
 */

#include "account_common.h"

#include <stdint.h>
#include <string.h>

typedef long ssize_t;

extern int geteuid(void);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int mkdir(const char *pathname, int mode);
extern int chown(const char *pathname, int owner, int group);
extern int stat(const char *pathname, void *statbuf);
extern int unlink(const char *pathname);
extern int rmdir(const char *pathname);
extern int open(const char *pathname, int flags, int mode);
extern int close(int fd);

#define SYS_GETDENTS64 217
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

static long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "cc", "memory");
    return ret;
}

int acct_require_root(void) {
    if (geteuid() != 0) {
        const char msg[] = "permission denied (root only)\n";
        (void)write(2, msg, sizeof(msg) - 1);
        return -1;
    }
    return 0;
}

int acct_mkdir_p(const char *path, int mode) {
    char buf[256];
    size_t j;
    size_t i;

    if (!path || path[0] != '/') {
        return -1;
    }
    j = 0;
    buf[j++] = '/';
    buf[j] = '\0';
    i = 1;
    while (path[i]) {
        while (path[i] == '/') {
            i++;
        }
        if (!path[i]) {
            break;
        }
        {
            size_t start = i;
            while (path[i] && path[i] != '/') {
                i++;
            }
            if (j + (i - start) + 2 >= sizeof(buf)) {
                return -1;
            }
            if (j > 0 && buf[j - 1] != '/') {
                buf[j++] = '/';
            }
            memcpy(buf + j, path + start, i - start);
            j += i - start;
            buf[j] = '\0';
        }
        {
            long r = mkdir(buf, mode);
            if (r < 0 && r != -17) { /* EEXIST */
                return (int)r;
            }
        }
    }
    return 0;
}

int acct_chown_tree(const char *path, int uid, int gid) {
    struct ob_stat st;
    long fd;
    char dbuf[1024];
    long nread;
    long bpos;

    if (chown(path, uid, gid) < 0) {
        return -1;
    }
    if (stat(path, &st) < 0) {
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        return 0;
    }
    fd = syscall3(2, (long)path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return 0;
    }
    while (1) {
        nread = syscall3(SYS_GETDENTS64, fd, (long)dbuf, sizeof(dbuf));
        if (nread <= 0) {
            break;
        }
        bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(dbuf + bpos);
            if (d->d_name[0] &&
                !(d->d_name[0] == '.' && d->d_name[1] == '\0') &&
                !(d->d_name[0] == '.' && d->d_name[1] == '.' && d->d_name[2] == '\0')) {
                char child[512];
                size_t plen = strlen(path);
                size_t nlen = strlen(d->d_name);
                if (plen + 1 + nlen + 1 < sizeof(child)) {
                    memcpy(child, path, plen);
                    if (plen > 0 && child[plen - 1] != '/') {
                        child[plen++] = '/';
                    }
                    memcpy(child + plen, d->d_name, nlen + 1);
                    acct_chown_tree(child, uid, gid);
                }
            }
            if (d->d_reclen == 0) {
                break;
            }
            bpos += d->d_reclen;
        }
    }
    close((int)fd);
    return 0;
}

int acct_prompt_password(const char *prompt, char *buf, size_t cap) {
    ssize_t n;
    size_t len;

    if (cap < 2) {
        return -1;
    }
    (void)write(1, prompt, strlen(prompt));
    n = read(0, buf, cap - 1);
    if (n < 0) {
        return -1;
    }
    len = (size_t)n;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        len--;
    }
    buf[len] = '\0';
    return 0;
}

static int rm_tree_inner(const char *path) {
    struct ob_stat st;
    long fd;
    char dbuf[1024];
    long nread;
    long bpos;

    if (stat(path, &st) < 0) {
        return -1;
    }
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        return unlink(path);
    }
    fd = syscall3(2, (long)path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return (int)fd;
    }
    while (1) {
        nread = syscall3(SYS_GETDENTS64, fd, (long)dbuf, sizeof(dbuf));
        if (nread <= 0) {
            break;
        }
        bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(dbuf + bpos);
            if (d->d_name[0] &&
                !(d->d_name[0] == '.' && d->d_name[1] == '\0') &&
                !(d->d_name[0] == '.' && d->d_name[1] == '.' && d->d_name[2] == '\0')) {
                char child[512];
                size_t plen = strlen(path);
                size_t nlen = strlen(d->d_name);
                if (plen + 1 + nlen + 1 < sizeof(child)) {
                    memcpy(child, path, plen);
                    if (plen > 0 && child[plen - 1] != '/') {
                        child[plen++] = '/';
                    }
                    memcpy(child + plen, d->d_name, nlen + 1);
                    rm_tree_inner(child);
                }
            }
            if (d->d_reclen == 0) {
                break;
            }
            bpos += d->d_reclen;
        }
    }
    close((int)fd);
    return rmdir(path);
}

int acct_rm_tree(const char *path) {
    return rm_tree_inner(path);
}
