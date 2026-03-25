/*
 * Obelisk OS - Minimal BusyBox-style Userland
 * From Axioms, Order.
 */

#include <stdint.h>
#include <stddef.h>

typedef long ssize_t;

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_STAT    4
#define SYS_IOCTL   16
#define SYS_PIPE    22
#define SYS_DUP     32
#define SYS_DUP2    33
#define SYS_FORK    57
#define SYS_EXECVE  59
#define SYS_EXIT    60
#define SYS_WAIT4   61
#define SYS_UNAME   63
#define SYS_GETDENTS64 217
#define SYS_GETCWD  79
#define SYS_CHDIR   80
#define SYS_MKDIR   83
#define SYS_RMDIR   84
#define SYS_UNLINK  87
#define SYS_CHMOD   90
#define SYS_CHOWN   92
#define SYS_GETUID  102
#define SYS_GETGID  104
#define SYS_GETEUID 107
#define SYS_GETEGID 108
#define SYS_SETUID  105
#define SYS_SETGID  106
#define SYS_REBOOT  169
#define SYS_OBELISK_SYSCTL 400
#define AT_NULL     0
#define AT_EXECFN   31
#define TIOCGWINSZ  0x5413

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_CREAT     0x0040
#define O_APPEND    0x0400
#define O_TRUNC     0x0200
#define O_DIRECTORY 0x10000

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
} __attribute__((packed));

struct ob_stat {
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

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

static char env_home[96] = "HOME=/";
static char env_shell[96] = "SHELL=/bin/sh";
static char env_user[64] = "USER=root";
static char *shell_envp[] = {
    "PATH=/bin:/sbin:/usr/bin",
    env_home,
    env_shell,
    env_user,
    "TERM=vt100",
    NULL
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct ob_sysctl_args {
    const char *name;
    void *oldval;
    size_t *oldlenp;
    const void *newval;
    size_t newlen;
};

static int applet_main(const char *name, int argc, char **argv);
static int is_builtin_applet_name(const char *name);

static inline long syscall0(long num) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "cc", "memory"
    );
    return ret;
}

static inline long syscall1(long num, long arg1) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "cc", "memory"
    );
    return ret;
}

static inline long syscall2(long num, long arg1, long arg2) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2)
        : "cc", "memory"
    );
    return ret;
}

static inline long syscall3(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "cc", "memory"
    );
    return ret;
}

static inline long syscall4(long num, long arg1, long arg2, long arg3, long arg4) {
    long ret;
    register long r10 __asm__("r10") = arg4;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "cc", "memory"
    );
    return ret;
}

static size_t str_len(const char *s) {
    size_t len = 0;
    while (s && s[len]) {
        len++;
    }
    return len;
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int str_ncmp(const char *a, const char *b, size_t n) {
    while (n > 0 && *a && *b && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static void str_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0) return;
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void str_append(char *dst, const char *src, size_t cap) {
    size_t dlen = str_len(dst);
    if (dlen >= cap) return;
    str_copy(dst + dlen, src, cap - dlen);
}

static void build_env_kv(char *dst, size_t cap, const char *key, const char *val) {
    if (cap == 0) return;
    dst[0] = '\0';
    str_copy(dst, key, cap);
    str_append(dst, "=", cap);
    str_append(dst, val ? val : "", cap);
}

static void write_ch(char c) {
    syscall3(SYS_WRITE, 1, (long)&c, 1);
}

static const char *base_name(const char *path) {
    const char *name = path;
    while (*path) {
        if (*path == '/') {
            name = path + 1;
        }
        path++;
    }
    return name;
}

static void write_str(const char *s) {
    syscall3(SYS_WRITE, 1, (long)s, (long)str_len(s));
}

static const char *lookup_env_value(const char *key) {
    size_t klen = str_len(key);
    for (int i = 0; shell_envp[i]; i++) {
        const char *kv = shell_envp[i];
        const char *eq = kv;
        while (*eq && *eq != '=') {
            eq++;
        }
        if (*eq != '=') {
            continue;
        }
        if ((size_t)(eq - kv) == klen && str_ncmp(kv, key, klen) == 0) {
            return eq + 1;
        }
    }
    return "";
}

static int parse_words_shell(const char *line, char out[][256], char **argv,
                             int max_args, int expand_vars) {
    int argc = 0;
    const char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }

        char *dst = out[argc];
        size_t dcap = 255;
        int in_single = 0;
        int in_double = 0;

        while (*p) {
            char c = *p++;
            if (!in_single && c == '"') {
                in_double = !in_double;
                continue;
            }
            if (!in_double && c == '\'') {
                in_single = !in_single;
                continue;
            }
            if (!in_single && c == '\\' && *p) {
                c = *p++;
                if (dcap > 0) {
                    *dst++ = c;
                    dcap--;
                }
                continue;
            }
            if (!in_single && c == '$' && expand_vars) {
                char key[64];
                size_t k = 0;
                if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_') {
                    while (((*p >= 'A' && *p <= 'Z') ||
                            (*p >= 'a' && *p <= 'z') ||
                            (*p >= '0' && *p <= '9') ||
                            *p == '_') && k + 1 < sizeof(key)) {
                        key[k++] = *p++;
                    }
                    key[k] = '\0';
                    const char *val = lookup_env_value(key);
                    while (*val && dcap > 0) {
                        *dst++ = *val++;
                        dcap--;
                    }
                    continue;
                }
            }
            if (!in_single && !in_double && (c == ' ' || c == '\t')) {
                break;
            }
            if (dcap > 0) {
                *dst++ = c;
                dcap--;
            }
        }
        *dst = '\0';
        argv[argc] = out[argc];
        argc++;
    }
    return argc;
}

static void print_errno(const char *op, long err) {
    write_str(op);
    write_str(": failed (");
    if (err < 0) {
        err = -err;
    }
    char tmp[24];
    int n = 0;
    if (err == 0) {
        tmp[n++] = '0';
    } else {
        char rev[24];
        int r = 0;
        while (err > 0 && r < (int)sizeof(rev)) {
            rev[r++] = (char)('0' + (err % 10));
            err /= 10;
        }
        while (r > 0) {
            tmp[n++] = rev[--r];
        }
    }
    tmp[n] = '\0';
    write_str(tmp);
    write_str(")\n");
}

static void applet_pwd(void) {
    char buf[256];
    long ret = syscall2(SYS_GETCWD, (long)buf, sizeof(buf));
    if (ret < 0) {
        print_errno("pwd", ret);
        return;
    }
    write_str(buf);
    write_str("\n");
}

static void applet_cd(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";
    long ret = syscall1(SYS_CHDIR, (long)path);
    if (ret < 0) {
        print_errno("cd", ret);
    }
}

static int get_term_cols(void) {
    struct winsize ws;
    long ret;
    ws.ws_col = 0;
    ret = syscall3(SYS_IOCTL, 1, TIOCGWINSZ, (long)&ws);
    if (ret < 0 || ws.ws_col == 0) {
        return 80;
    }
    return (int)ws.ws_col;
}

static void print_spaces(int n) {
    while (n-- > 0) {
        write_ch(' ');
    }
}

static void write_u64_dec(uint64_t v);

static void mode_string(uint32_t mode, char out[11]) {
    /* File type */
    uint32_t type = mode & 0170000;
    out[0] = (type == 0040000) ? 'd' :
             (type == 0120000) ? 'l' :
             (type == 0100000) ? '-' : '?';
    /* Permissions */
    out[1] = (mode & 0400) ? 'r' : '-';
    out[2] = (mode & 0200) ? 'w' : '-';
    out[3] = (mode & 0100) ? 'x' : '-';
    out[4] = (mode & 0040) ? 'r' : '-';
    out[5] = (mode & 0020) ? 'w' : '-';
    out[6] = (mode & 0010) ? 'x' : '-';
    out[7] = (mode & 0004) ? 'r' : '-';
    out[8] = (mode & 0002) ? 'w' : '-';
    out[9] = (mode & 0001) ? 'x' : '-';
    out[10] = '\0';
}

static void ls_print_long(const char *dirpath, const char *name) {
    struct ob_stat st;
    char full[512];
    size_t plen = str_len(dirpath);
    size_t nlen = str_len(name);

    /* Build "<dir>/<name>" (handle "." specially). */
    if (dirpath[0] == '.' && dirpath[1] == '\0') {
        if (nlen + 1 > sizeof(full)) {
            return;
        }
        str_copy(full, name, sizeof(full));
    } else {
        if (plen + 1 + nlen + 1 > sizeof(full)) {
            return;
        }
        str_copy(full, dirpath, sizeof(full));
        if (plen > 0 && full[plen - 1] != '/') {
            full[plen++] = '/';
            full[plen] = '\0';
        }
        str_copy(full + plen, name, sizeof(full) - plen);
    }

    long ret = syscall2(SYS_STAT, (long)full, (long)&st);
    if (ret < 0) {
        /* If stat fails, still show the name. */
        write_str("?????????? ");
        write_str(name);
        write_ch('\n');
        return;
    }

    char ms[11];
    mode_string(st.st_mode, ms);
    write_str(ms);
    write_ch(' ');
    write_u64_dec(st.st_uid);
    write_ch(' ');
    write_u64_dec((uint64_t)st.st_size);
    write_ch(' ');
    write_str(name);
    write_ch('\n');
}

static void applet_ls(int argc, char **argv) {
    int show_all = 0;
    int long_fmt = 0;
    int first = 1;

    while (first < argc && argv[first][0] == '-' && argv[first][1] != '\0') {
        if (argv[first][1] == '-' && argv[first][2] == '\0') {
            first++;
            break;
        }
        const char *o = argv[first] + 1;
        while (*o) {
            if (*o == 'a') show_all = 1;
            else if (*o == 'l') long_fmt = 1;
            o++;
        }
        first++;
    }

    const char *path = (first < argc) ? argv[first] : ".";
    long fd = syscall3(SYS_OPEN, (long)path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        print_errno("ls", fd);
        return;
    }

    char buf[1024];
    if (long_fmt) {
        while (1) {
            long nread = syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
            if (nread < 0) {
                print_errno("ls", nread);
                break;
            }
            if (nread == 0) {
                break;
            }
            long bpos = 0;
            while (bpos < nread) {
                struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
                int include = show_all ? 1 : (d->d_name[0] != '.');
                if (include) {
                    ls_print_long(path, d->d_name);
                }
                if (d->d_reclen == 0) {
                    break;
                }
                bpos += d->d_reclen;
            }
        }
        syscall1(SYS_CLOSE, fd);
        return;
    }

    char names[256][128];
    int namec = 0;
    int maxw = 0;
    int overflow = 0;
    while (1) {
        long nread = syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
        if (nread < 0) {
            print_errno("ls", nread);
            break;
        }
        if (nread == 0) {
            break;
        }
        long bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            int include = show_all ? 1 : (d->d_name[0] != '.');
            if (include) {
                if (namec < 256) {
                    str_copy(names[namec], d->d_name, sizeof(names[0]));
                    int w = (int)str_len(names[namec]);
                    if (w > maxw) {
                        maxw = w;
                    }
                    namec++;
                } else {
                    overflow = 1;
                }
            }
            if (d->d_reclen == 0) {
                break;
            }
            bpos += d->d_reclen;
        }
    }
    syscall1(SYS_CLOSE, fd);

    if (namec == 0) {
        return;
    }
    if (overflow) {
        for (int i = 0; i < namec; i++) {
            write_str(names[i]);
            write_ch('\n');
        }
        return;
    }

    int cols = get_term_cols();
    int colw = maxw + 2;
    if (colw < 1) {
        colw = 1;
    }
    int ncols = cols / colw;
    if (ncols < 1) {
        ncols = 1;
    }
    int rows = (namec + ncols - 1) / ncols;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < ncols; c++) {
            int idx = c * rows + r;
            if (idx >= namec) {
                continue;
            }
            write_str(names[idx]);
            if (c + 1 < ncols) {
                int pad = colw - (int)str_len(names[idx]);
                if (pad < 1) {
                    pad = 1;
                }
                print_spaces(pad);
            }
        }
        write_ch('\n');
    }
}

static void applet_cat(int argc, char **argv) {
    if (argc < 2) {
        char buf[512];
        while (1) {
            long n = syscall3(SYS_READ, 0, (long)buf, sizeof(buf));
            if (n < 0) {
                if (n == -4) { /* EINTR */
                    return;
                }
                print_errno("cat", n);
                break;
            }
            if (n == 0) {
                break;
            }
            syscall3(SYS_WRITE, 1, (long)buf, n);
        }
        return;
    }
    for (int i = 1; i < argc; i++) {
        long fd = syscall3(SYS_OPEN, (long)argv[i], O_RDONLY, 0);
        if (fd < 0) {
            print_errno("cat", fd);
            continue;
        }
        char buf[512];
        while (1) {
            long n = syscall3(SYS_READ, fd, (long)buf, sizeof(buf));
            if (n < 0) {
                print_errno("cat", n);
                break;
            }
            if (n == 0) {
                break;
            }
            syscall3(SYS_WRITE, 1, (long)buf, n);
        }
        syscall1(SYS_CLOSE, fd);
    }
}

static void applet_touch(int argc, char **argv) {
    if (argc < 2) {
        write_str("touch: missing operand\n");
        return;
    }
    for (int i = 1; i < argc; i++) {
        long fd = syscall3(SYS_OPEN, (long)argv[i], O_CREAT | O_WRONLY, 0644);
        if (fd < 0) {
            print_errno("touch", fd);
            continue;
        }
        syscall1(SYS_CLOSE, fd);
    }
}

static void applet_mkdir(int argc, char **argv) {
    if (argc < 2) {
        write_str("mkdir: missing operand\n");
        return;
    }
    for (int i = 1; i < argc; i++) {
        long ret = syscall2(SYS_MKDIR, (long)argv[i], 0755);
        if (ret < 0) {
            print_errno("mkdir", ret);
        }
    }
}

static int rm_recursive_path(const char *path, int force) {
    long fd = syscall3(SYS_OPEN, (long)path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        long ret = syscall1(SYS_UNLINK, (long)path);
        if (ret < 0 && !(force && ret == -2)) { /* ENOENT */
            print_errno("rm", ret);
            return (int)ret;
        }
        return 0;
    }

    char buf[1024];
    while (1) {
        long nread = syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
        if (nread <= 0) break;
        long bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            if (d->d_name[0] &&
                !(d->d_name[0] == '.' && d->d_name[1] == '\0') &&
                !(d->d_name[0] == '.' && d->d_name[1] == '.' && d->d_name[2] == '\0')) {
                char child[512];
                size_t plen = str_len(path);
                size_t nlen = str_len(d->d_name);
                if (plen + 1 + nlen + 1 < sizeof(child)) {
                    str_copy(child, path, sizeof(child));
                    if (plen > 0 && child[plen - 1] != '/') {
                        child[plen++] = '/';
                        child[plen] = '\0';
                    }
                    str_copy(child + plen, d->d_name, sizeof(child) - plen);
                    rm_recursive_path(child, force);
                }
            }
            if (d->d_reclen == 0) break;
            bpos += d->d_reclen;
        }
    }
    syscall1(SYS_CLOSE, fd);
    long ret = syscall1(SYS_RMDIR, (long)path);
    if (ret < 0 && !(force && ret == -2)) {
        print_errno("rm", ret);
        return (int)ret;
    }
    return 0;
}

static void applet_rm(int argc, char **argv) {
    int recursive = 0;
    int force = 0;
    int first = 1;
    while (first < argc && argv[first][0] == '-') {
        const char *o = argv[first] + 1;
        while (*o) {
            if (*o == 'r' || *o == 'R') recursive = 1;
            else if (*o == 'f') force = 1;
            o++;
        }
        first++;
    }
    if (first >= argc) {
        write_str("rm: missing operand\n");
        return;
    }
    for (int i = first; i < argc; i++) {
        long ret = syscall1(SYS_UNLINK, (long)argv[i]);
        if (ret == -21 && recursive) { /* EISDIR */
            rm_recursive_path(argv[i], force);
            continue;
        }
        if (ret < 0 && !(force && ret == -2)) { /* ENOENT */
            print_errno("rm", ret);
        }
    }
}

static void applet_rmdir(int argc, char **argv) {
    if (argc < 2) {
        write_str("rmdir: missing operand\n");
        return;
    }
    for (int i = 1; i < argc; i++) {
        long ret = syscall1(SYS_RMDIR, (long)argv[i]);
        if (ret < 0) {
            print_errno("rmdir", ret);
        }
    }
}

static void applet_write(int argc, char **argv) {
    if (argc < 3) {
        write_str("write: usage: write <file> <text...>\n");
        return;
    }
    long fd = syscall3(SYS_OPEN, (long)argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        print_errno("write", fd);
        return;
    }
    for (int i = 2; i < argc; i++) {
        const char *p = argv[i];
        while (*p) {
            if (p[0] == '\\' && p[1] == 'n') {
                syscall3(SYS_WRITE, fd, (long)"\n", 1);
                p += 2;
                continue;
            }
            if (p[0] == '\\' && p[1] == 't') {
                syscall3(SYS_WRITE, fd, (long)"\t", 1);
                p += 2;
                continue;
            }
            syscall3(SYS_WRITE, fd, (long)p, 1);
            p++;
        }
        if (i + 1 < argc) {
            syscall3(SYS_WRITE, fd, (long)" ", 1);
        }
    }
    syscall3(SYS_WRITE, fd, (long)"\n", 1);
    syscall1(SYS_CLOSE, fd);
}

static void applet_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        write_str(argv[i]);
        if (i + 1 < argc) {
            write_str(" ");
        }
    }
    write_str("\n");
}

static void applet_uname(int argc, char **argv) {
    struct utsname u;
    int show_s = 0, show_n = 0, show_r = 0, show_v = 0, show_m = 0, show_a = 0;
    int first = 1;
    long ret = syscall1(SYS_UNAME, (long)&u);
    if (ret < 0) {
        write_str("uname: syscall failed\n");
        return;
    }
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') continue;
        for (int j = 1; argv[i][j]; j++) {
            char o = argv[i][j];
            if (o == 'a') show_a = 1;
            else if (o == 's') show_s = 1;
            else if (o == 'n') show_n = 1;
            else if (o == 'r') show_r = 1;
            else if (o == 'v') show_v = 1;
            else if (o == 'm') show_m = 1;
            else {
                write_str("uname: unsupported option\n");
                return;
            }
        }
    }
    if (show_a) {
        show_s = show_n = show_r = show_v = show_m = 1;
    }
    if (!show_s && !show_n && !show_r && !show_v && !show_m) {
        show_s = 1;
    }
    if (show_s) {
        if (!first) write_str(" ");
        write_str(u.sysname);
        first = 0;
    }
    if (show_n) {
        if (!first) write_str(" ");
        write_str(u.nodename);
        first = 0;
    }
    if (show_r) {
        if (!first) write_str(" ");
        write_str(u.release);
        first = 0;
    }
    if (show_v) {
        if (!first) write_str(" ");
        write_str(u.version);
        first = 0;
    }
    if (show_m) {
        if (!first) write_str(" ");
        write_str(u.machine);
        first = 0;
    }
    write_str("\n");
}

static void applet_reboot(void) {
    long ret = syscall4(SYS_REBOOT, 0xfee1dead, 0x28121969, 0x01234567, 0);
    if (ret < 0) {
        write_str("reboot: syscall failed\n");
    }
}

static void applet_shutdown(void) {
    long ret = syscall4(SYS_REBOOT, 0xfee1dead, 0x28121969, 0x4321FEDC, 0);
    if (ret < 0) {
        write_str("shutdown: syscall failed\n");
    }
}

static int lookup_uid_name(unsigned uid, char *name_out, size_t cap);
static int parse_uint10(const char *s, unsigned *out);

static void write_u64_dec(uint64_t v) {
    char tmp[24];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        char rev[24];
        int r = 0;
        while (v > 0 && r < (int)sizeof(rev)) {
            rev[r++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (r > 0) {
            tmp[n++] = rev[--r];
        }
    }
    tmp[n] = '\0';
    write_str(tmp);
}

static void applet_whoami(void) {
    long uid = syscall0(SYS_GETUID);
    char uname[32];
    if (uid >= 0 && lookup_uid_name((unsigned)uid, uname, sizeof(uname)) == 0) {
        write_str(uname);
    } else {
        write_u64_dec(uid >= 0 ? (uint64_t)uid : 0);
    }
    write_str("\n");
}

static void applet_id(void) {
    long uid = syscall0(SYS_GETUID);
    long euid = syscall0(SYS_GETEUID);
    long gid = syscall0(SYS_GETGID);
    long egid = syscall0(SYS_GETEGID);
    write_str("uid=");
    write_u64_dec(uid >= 0 ? (uint64_t)uid : 0);
    write_str(" euid=");
    write_u64_dec(euid >= 0 ? (uint64_t)euid : 0);
    write_str(" gid=");
    write_u64_dec(gid >= 0 ? (uint64_t)gid : 0);
    write_str(" egid=");
    write_u64_dec(egid >= 0 ? (uint64_t)egid : 0);
    write_str("\n");
}

static int is_ws_char(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f');
}

static int parse_count_arg(const char *s, int *out) {
    unsigned v = 0;
    if (!s || !*s) return -1;
    if (parse_uint10(s, &v) < 0) return -1;
    if (v > 1000000U) v = 1000000U;
    *out = (int)v;
    return 0;
}

static int parse_n_option(int argc, char **argv, int *first_file, int *n_out, const char *cmd) {
    int first = 1;
    int n = 10;

    if (first < argc && argv[first][0] == '-') {
        if (argv[first][1] == 'n' && argv[first][2] == '\0') {
            if (first + 1 >= argc || parse_count_arg(argv[first + 1], &n) < 0) {
                write_str(cmd);
                write_str(": invalid -n value\n");
                return -1;
            }
            first += 2;
        } else if (argv[first][1] == 'n' && argv[first][2] != '\0') {
            if (parse_count_arg(argv[first] + 2, &n) < 0) {
                write_str(cmd);
                write_str(": invalid -n value\n");
                return -1;
            }
            first += 1;
        } else if (argv[first][1] >= '0' && argv[first][1] <= '9') {
            if (parse_count_arg(argv[first] + 1, &n) < 0) {
                write_str(cmd);
                write_str(": invalid count\n");
                return -1;
            }
            first += 1;
        }
    }

    *first_file = first;
    *n_out = n;
    return 0;
}

static int arg_is_help(const char *s) {
    if (!s) return 0;
    return (str_cmp(s, "-h") == 0 || str_cmp(s, "--help") == 0);
}

static void applet_head_stream_fd(long fd, int nlines) {
    if (nlines <= 0) return;
    char buf[256];
    int lines = 0;
    while (1) {
        long n = syscall3(SYS_READ, fd, (long)buf, sizeof(buf));
        if (n < 0) {
            if (n == -4) { /* EINTR */
                continue;
            }
            print_errno("head", n);
            return;
        }
        if (n == 0) {
            return;
        }
        for (long i = 0; i < n; i++) {
            syscall3(SYS_WRITE, 1, (long)&buf[i], 1);
            if (buf[i] == '\n') {
                lines++;
                if (lines >= nlines) {
                    return;
                }
            }
        }
    }
}

static void applet_head(int argc, char **argv) {
    if (argc > 1 && arg_is_help(argv[1])) {
        write_str("usage: head [-n N] [file...]\n");
        return;
    }
    int first = 1;
    int nlines = 10;
    if (parse_n_option(argc, argv, &first, &nlines, "head") < 0) {
        return;
    }
    if (first >= argc) {
        applet_head_stream_fd(0, nlines);
        return;
    }
    for (int i = first; i < argc; i++) {
        long fd = syscall3(SYS_OPEN, (long)argv[i], O_RDONLY, 0);
        if (fd < 0) {
            print_errno("head", fd);
            continue;
        }
        applet_head_stream_fd(fd, nlines);
        syscall1(SYS_CLOSE, fd);
    }
}

static void applet_tail_from_buffer(const char *data, int len, int nlines) {
    if (len <= 0 || nlines <= 0) {
        return;
    }
    int start = 0;
    int lines = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (data[i] == '\n') {
            lines++;
            if (lines > nlines) {
                start = i + 1;
                break;
            }
        }
    }
    if (start < len) {
        syscall3(SYS_WRITE, 1, (long)(data + start), (size_t)(len - start));
    }
}

static void applet_tail_stream_fd(long fd, int nlines) {
    char ring[8192];
    int used = 0;
    int head = 0;
    while (1) {
        char buf[256];
        long n = syscall3(SYS_READ, fd, (long)buf, sizeof(buf));
        if (n < 0) {
            if (n == -4) { /* EINTR */
                continue;
            }
            print_errno("tail", n);
            return;
        }
        if (n == 0) {
            break;
        }
        for (long i = 0; i < n; i++) {
            ring[head] = buf[i];
            head = (head + 1) % (int)sizeof(ring);
            if (used < (int)sizeof(ring)) {
                used++;
            }
        }
    }
    if (used == 0) return;
    char flat[8192];
    int start = (head - used + (int)sizeof(ring)) % (int)sizeof(ring);
    for (int i = 0; i < used; i++) {
        flat[i] = ring[(start + i) % (int)sizeof(ring)];
    }
    applet_tail_from_buffer(flat, used, nlines);
}

static void applet_tail(int argc, char **argv) {
    if (argc > 1 && arg_is_help(argv[1])) {
        write_str("usage: tail [-n N] [file...]\n");
        return;
    }
    int first = 1;
    int nlines = 10;
    if (parse_n_option(argc, argv, &first, &nlines, "tail") < 0) {
        return;
    }
    if (first >= argc) {
        applet_tail_stream_fd(0, nlines);
        return;
    }
    for (int i = first; i < argc; i++) {
        long fd = syscall3(SYS_OPEN, (long)argv[i], O_RDONLY, 0);
        if (fd < 0) {
            print_errno("tail", fd);
            continue;
        }
        applet_tail_stream_fd(fd, nlines);
        syscall1(SYS_CLOSE, fd);
    }
}

static void wc_stream_fd(long fd, uint64_t *lines, uint64_t *words, uint64_t *bytes) {
    char buf[256];
    int in_word = 0;
    while (1) {
        long n = syscall3(SYS_READ, fd, (long)buf, sizeof(buf));
        if (n < 0) {
            if (n == -4) { /* EINTR */
                continue;
            }
            print_errno("wc", n);
            return;
        }
        if (n == 0) {
            return;
        }
        *bytes += (uint64_t)n;
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') (*lines)++;
            if (is_ws_char(c)) {
                in_word = 0;
            } else if (!in_word) {
                (*words)++;
                in_word = 1;
            }
        }
    }
}

static void wc_print_counts(int show_l, int show_w, int show_c,
                            uint64_t l, uint64_t w, uint64_t c, const char *name) {
    if (show_l) {
        write_u64_dec(l);
        write_ch(' ');
    }
    if (show_w) {
        write_u64_dec(w);
        write_ch(' ');
    }
    if (show_c) {
        write_u64_dec(c);
        write_ch(' ');
    }
    if (name && *name) {
        write_str(name);
    }
    write_ch('\n');
}

static void applet_wc(int argc, char **argv) {
    if (argc > 1 && arg_is_help(argv[1])) {
        write_str("usage: wc [-lwc] [file...]\n");
        return;
    }
    int show_l = 0, show_w = 0, show_c = 0;
    int first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1] != '\0') {
        const char *o = argv[first] + 1;
        while (*o) {
            if (*o == 'l') show_l = 1;
            else if (*o == 'w') show_w = 1;
            else if (*o == 'c') show_c = 1;
            else {
                write_str("wc: usage: wc [-lwc] [file...]\n");
                return;
            }
            o++;
        }
        first++;
    }
    if (!show_l && !show_w && !show_c) {
        show_l = show_w = show_c = 1;
    }

    if (first >= argc) {
        uint64_t l = 0, w = 0, c = 0;
        wc_stream_fd(0, &l, &w, &c);
        wc_print_counts(show_l, show_w, show_c, l, w, c, NULL);
        return;
    }
    for (int i = first; i < argc; i++) {
        uint64_t l = 0, w = 0, c = 0;
        long fd = syscall3(SYS_OPEN, (long)argv[i], O_RDONLY, 0);
        if (fd < 0) {
            print_errno("wc", fd);
            continue;
        }
        wc_stream_fd(fd, &l, &w, &c);
        syscall1(SYS_CLOSE, fd);
        wc_print_counts(show_l, show_w, show_c, l, w, c, argv[i]);
    }
}

static void cut_stream_fd(long fd, char delim, int field) {
    char line[512];
    int len = 0;
    while (1) {
        char c = 0;
        long n = syscall3(SYS_READ, fd, (long)&c, 1);
        if (n < 0) {
            if (n == -4) { /* EINTR */
                continue;
            }
            print_errno("cut", n);
            return;
        }
        if (n == 0) {
            if (len > 0) {
                line[len] = '\0';
                int cur_field = 1;
                int start = 0;
                int end = len;
                for (int i = 0; i <= len; i++) {
                    if (i == len || line[i] == delim) {
                        if (cur_field == field) {
                            start = start;
                            end = i;
                            break;
                        }
                        cur_field++;
                        start = i + 1;
                    }
                }
                if (field <= cur_field && end >= start) {
                    syscall3(SYS_WRITE, 1, (long)(line + start), (size_t)(end - start));
                }
                write_ch('\n');
            }
            return;
        }
        if (c == '\n') {
            line[len] = '\0';
            int cur_field = 1;
            int start = 0;
            int end = len;
            for (int i = 0; i <= len; i++) {
                if (i == len || line[i] == delim) {
                    if (cur_field == field) {
                        end = i;
                        break;
                    }
                    cur_field++;
                    start = i + 1;
                }
            }
            if (field <= cur_field && end >= start) {
                syscall3(SYS_WRITE, 1, (long)(line + start), (size_t)(end - start));
            }
            write_ch('\n');
            len = 0;
            continue;
        }
        if (len + 1 < (int)sizeof(line)) {
            line[len++] = c;
        }
    }
}

static void applet_cut(int argc, char **argv) {
    if (argc > 1 && arg_is_help(argv[1])) {
        write_str("usage: cut -d <char> -f <field> [file...]\n");
        return;
    }
    int first = 1;
    char delim = '\t';
    int field = 0;
    while (first < argc && argv[first][0] == '-' && argv[first][1] != '\0') {
        if (str_cmp(argv[first], "-d") == 0) {
            if (first + 1 >= argc || argv[first + 1][0] == '\0') {
                write_str("cut: usage: cut -d <char> -f <field> [file...]\n");
                return;
            }
            delim = argv[first + 1][0];
            first += 2;
            continue;
        }
        if (str_cmp(argv[first], "-f") == 0) {
            unsigned fv = 0;
            if (first + 1 >= argc || parse_uint10(argv[first + 1], &fv) < 0 || fv == 0) {
                write_str("cut: usage: cut -d <char> -f <field> [file...]\n");
                return;
            }
            field = (int)fv;
            first += 2;
            continue;
        }
        write_str("cut: usage: cut -d <char> -f <field> [file...]\n");
        return;
    }
    if (field <= 0) {
        write_str("cut: usage: cut -d <char> -f <field> [file...]\n");
        return;
    }
    if (first >= argc) {
        cut_stream_fd(0, delim, field);
        return;
    }
    for (int i = first; i < argc; i++) {
        long fd = syscall3(SYS_OPEN, (long)argv[i], O_RDONLY, 0);
        if (fd < 0) {
            print_errno("cut", fd);
            continue;
        }
        cut_stream_fd(fd, delim, field);
        syscall1(SYS_CLOSE, fd);
    }
}

static void applet_users(void) {
    applet_whoami();
}

static int parse_uint8(const char *s, unsigned *out) {
    unsigned v = 0;
    int n = 0;
    while (s[n] >= '0' && s[n] <= '7') {
        v = (v << 3) + (unsigned)(s[n] - '0');
        n++;
    }
    if (n == 0 || s[n] != '\0') return -1;
    *out = v;
    return 0;
}

static void applet_chmod(int argc, char **argv) {
    if (argc < 3) {
        write_str("chmod: usage: chmod <mode-octal> <path...>\n");
        return;
    }
    unsigned mode = 0;
    if (parse_uint8(argv[1], &mode) < 0) {
        write_str("chmod: mode must be octal (e.g. 755)\n");
        return;
    }
    for (int i = 2; i < argc; i++) {
        long ret = syscall2(SYS_CHMOD, (long)argv[i], (long)(mode & 07777));
        if (ret < 0) {
            print_errno("chmod", ret);
        }
    }
}

static int parse_user_or_group(const char *s, unsigned *id_out) {
    if (parse_uint10(s, id_out) == 0) {
        return 0;
    }
    if (str_cmp(s, "root") == 0) {
        *id_out = 0;
        return 0;
    }
    if (str_cmp(s, "obelisk") == 0) {
        *id_out = 1000;
        return 0;
    }
    return -1;
}

static void applet_chown(int argc, char **argv) {
    if (argc < 3) {
        write_str("chown: usage: chown <uid[:gid]|user[:group]> <path...>\n");
        return;
    }
    char spec[64];
    char *sep;
    unsigned uid = 0;
    unsigned gid = 0;
    str_copy(spec, argv[1], sizeof(spec));
    sep = spec;
    while (*sep && *sep != ':') {
        sep++;
    }
    if (*sep == ':') {
        *sep++ = '\0';
    } else {
        sep = NULL;
    }
    if (parse_user_or_group(spec, &uid) < 0) {
        write_str("chown: invalid owner\n");
        return;
    }
    if (sep) {
        if (parse_user_or_group(sep, &gid) < 0) {
            write_str("chown: invalid group\n");
            return;
        }
    } else {
        gid = uid;
    }
    for (int i = 2; i < argc; i++) {
        long ret = syscall3(SYS_CHOWN, (long)argv[i], (long)uid, (long)gid);
        if (ret < 0) {
            print_errno("chown", ret);
        }
    }
}

static void applet_stat(int argc, char **argv) {
    if (argc < 2) {
        write_str("stat: usage: stat <path...>\n");
        return;
    }
    for (int i = 1; i < argc; i++) {
        struct ob_stat st;
        long ret;
        st.st_mode = 0;
        st.st_uid = 0;
        st.st_gid = 0;
        ret = syscall2(SYS_STAT, (long)argv[i], (long)&st);
        if (ret < 0) {
            print_errno("stat", ret);
            continue;
        }
        write_str(argv[i]);
        write_str(": uid=");
        write_u64_dec(st.st_uid);
        write_str(" gid=");
        write_u64_dec(st.st_gid);
        write_str(" mode=");
        write_u64_dec(st.st_mode & 07777);
        write_str(" type=");
        write_u64_dec(st.st_mode & 0170000);
        write_str("\n");
    }
}

static int try_exec_external(char **args);
static void report_exec_failure(const char *cmd, int ret);
static int wildcard_match(const char *pat, const char *s);

static int read_uptime_seconds(unsigned long *out_secs) {
    char buf[64];
    size_t len = sizeof(buf);
    struct ob_sysctl_args args;
    unsigned long v = 0;
    int i = 0;
    if (!out_secs) {
        return -1;
    }
    args.name = "system.kernel.uptime";
    args.oldval = buf;
    args.oldlenp = &len;
    args.newval = NULL;
    args.newlen = 0;
    if (syscall1(SYS_OBELISK_SYSCTL, (long)&args) < 0) {
        return -1;
    }
    while (buf[i] >= '0' && buf[i] <= '9') {
        v = (v * 10UL) + (unsigned long)(buf[i] - '0');
        i++;
    }
    *out_secs = v;
    return 0;
}

static int applet_time_exec(int argc, char **argv) {
    long pid;
    int status = 0;
    unsigned long start = 0;
    unsigned long end = 0;
    if (argc < 2) {
        write_str("time: usage: time <command> [args...]\n");
        return 1;
    }
    (void)read_uptime_seconds(&start);
    pid = syscall0(SYS_FORK);
    if (pid < 0) {
        print_errno("fork", pid);
        return 1;
    }
    if (pid == 0) {
        if (is_builtin_applet_name(argv[1])) {
            int rc = applet_main(argv[1], argc - 1, &argv[1]);
            syscall1(SYS_EXIT, rc);
        } else {
            int erc = try_exec_external(&argv[1]);
            if (erc < 0) {
                report_exec_failure(argv[1], erc);
                syscall1(SYS_EXIT, (erc == -2) ? 127 : 1);
            }
        }
        __builtin_unreachable();
    }
    while (1) {
        long ret = syscall4(SYS_WAIT4, pid, (long)&status, 0, 0);
        if (ret >= 0) {
            break;
        }
        if (ret == -4) {
            continue;
        }
        print_errno("wait4", ret);
        break;
    }
    (void)read_uptime_seconds(&end);
    write_str("real ");
    write_u64_dec((end >= start) ? (end - start) : 0);
    write_str("s\n");
    return (status == 0) ? 0 : 1;
}

static const char *find_basename(const char *path) {
    const char *bn = base_name(path);
    if (bn[0] == '\0' && path[0] == '/' && path[1] == '\0') {
        return "/";
    }
    return bn;
}

static void applet_find_walk(const char *path, const char *name_pat) {
    struct ob_stat st;
    long sret;
    long fd;
    char buf[1024];
    const char *bn = find_basename(path);
    if (!path || path[0] == '\0') {
        return;
    }
    if (!name_pat || wildcard_match(name_pat, bn)) {
        write_str(path);
        write_ch('\n');
    }

    sret = syscall2(SYS_STAT, (long)path, (long)&st);
    if (sret < 0) {
        return;
    }
    if ((st.st_mode & 0170000) != 0040000) {
        return;
    }

    fd = syscall3(SYS_OPEN, (long)path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return;
    }

    while (1) {
        long nread = syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
        if (nread < 0) {
            break;
        }
        if (nread == 0) {
            break;
        }
        long bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            if (!(d->d_name[0] == '.' &&
                  (d->d_name[1] == '\0' ||
                   (d->d_name[1] == '.' && d->d_name[2] == '\0')))) {
                char child[512];
                size_t plen = str_len(path);
                size_t nlen = str_len(d->d_name);
                if (plen + 1 + nlen + 1 < sizeof(child)) {
                    str_copy(child, path, sizeof(child));
                    if (!(plen == 1 && child[0] == '/')) {
                        child[plen++] = '/';
                        child[plen] = '\0';
                    }
                    str_copy(child + plen, d->d_name, sizeof(child) - plen);
                    applet_find_walk(child, name_pat);
                }
            }
            if (d->d_reclen == 0) {
                break;
            }
            bpos += d->d_reclen;
        }
    }
    syscall1(SYS_CLOSE, fd);
}

static void applet_find(int argc, char **argv) {
    const char *paths[16];
    int pathc = 0;
    const char *name_pat = NULL;
    for (int i = 1; i < argc; i++) {
        if (str_cmp(argv[i], "-name") == 0) {
            if (i + 1 >= argc) {
                write_str("find: missing pattern after -name\n");
                return;
            }
            name_pat = argv[++i];
            continue;
        }
        if (str_cmp(argv[i], "-h") == 0 || str_cmp(argv[i], "--help") == 0) {
            write_str("Usage: find [path ...] [-name pattern]\n");
            return;
        }
        if (argv[i][0] == '-') {
            write_str("find: unsupported option: ");
            write_str(argv[i]);
            write_ch('\n');
            return;
        }
        if (pathc < (int)(sizeof(paths) / sizeof(paths[0]))) {
            paths[pathc++] = argv[i];
        }
    }
    if (pathc == 0) {
        paths[pathc++] = ".";
    }
    for (int i = 0; i < pathc; i++) {
        applet_find_walk(paths[i], name_pat);
    }
}

static int run_external(char **args);
static int run_shell(const char *prompt);
static int try_exec_external(char **args);
static void report_exec_failure(const char *cmd, int ret);
static char *trim_ws_inplace(char *s);
static int applet_main(const char *name, int argc, char **argv);

static int is_builtin_applet_name(const char *name) {
    static const char *const names[] = {
        "busybox", "sh", "ash", "zsh", "echo", "uname", "pwd",
        "ls", "cat", "touch", "mkdir", "rm", "rmdir",
        "chmod", "chown", "stat", "head", "tail", "wc", "cut",
        "true", "false", "users", "find", "time",
        "reboot", "shutdown", "poweroff", "halt", "clear",
        "su", "sudo", "whoami", "id", NULL
    };
    for (int i = 0; names[i]; i++) {
        if (str_cmp(name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

struct user_record {
    char name[32];
    char pass[64];
    unsigned uid;
    unsigned gid;
    char home[64];
    char shell[64];
};

struct static_account {
    const char *name;
    unsigned uid;
    unsigned gid;
    const char *home;
    const char *shell;
};

static const struct static_account static_accounts[] = {
    { "root", 0, 0, "/root", "/bin/sh" },
    { "obelisk", 1000, 1000, "/home/obelisk", "/bin/sh" },
};

static int lookup_static_user_record(const char *username, struct user_record *out) {
    for (size_t i = 0; i < (sizeof(static_accounts) / sizeof(static_accounts[0])); i++) {
        if (str_cmp(static_accounts[i].name, username) == 0) {
            str_copy(out->name, static_accounts[i].name, sizeof(out->name));
            out->pass[0] = '\0';
            out->uid = static_accounts[i].uid;
            out->gid = static_accounts[i].gid;
            str_copy(out->home, static_accounts[i].home, sizeof(out->home));
            str_copy(out->shell, static_accounts[i].shell, sizeof(out->shell));
            return 0;
        }
    }
    return -1;
}

static int lookup_static_uid_name(unsigned uid, char *name_out, size_t cap) {
    for (size_t i = 0; i < (sizeof(static_accounts) / sizeof(static_accounts[0])); i++) {
        if (static_accounts[i].uid == uid) {
            str_copy(name_out, static_accounts[i].name, cap);
            return 0;
        }
    }
    return -1;
}

static int parse_uint10(const char *s, unsigned *out) {
    unsigned v = 0;
    int n = 0;
    while (s[n] >= '0' && s[n] <= '9') {
        v = (v * 10u) + (unsigned)(s[n] - '0');
        n++;
    }
    if (n == 0 || s[n] != '\0') return -1;
    *out = v;
    return 0;
}

static int read_small_file(const char *path, char *buf, size_t cap) {
    long fd = syscall3(SYS_OPEN, (long)path, O_RDONLY, 0);
    if (fd < 0) return (int)fd;
    size_t off = 0;
    while (off + 1 < cap) {
        long n = syscall3(SYS_READ, fd, (long)(buf + off), (long)(cap - 1 - off));
        if (n < 0) {
            syscall1(SYS_CLOSE, fd);
            return (int)n;
        }
        if (n == 0) break;
        off += (size_t)n;
    }
    buf[off] = '\0';
    syscall1(SYS_CLOSE, fd);
    return (int)off;
}

static int split_colon_fields(char *line, char **fields, int max_fields) {
    int n = 0;
    char *p = line;
    while (n < max_fields) {
        fields[n++] = p;
        while (*p && *p != ':') p++;
        if (*p != ':') break;
        *p++ = '\0';
    }
    return n;
}

static int has_wildcards(const char *s) {
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '*' || s[i] == '?') return 1;
    }
    return 0;
}

static int wildcard_match(const char *pat, const char *s) {
    if (!*pat) return !*s;
    if (*pat == '*') {
        while (*pat == '*') pat++;
        if (!*pat) return 1;
        while (*s) {
            if (wildcard_match(pat, s)) return 1;
            s++;
        }
        return 0;
    }
    if (*pat == '?') {
        return *s ? wildcard_match(pat + 1, s + 1) : 0;
    }
    if (*pat != *s) return 0;
    return wildcard_match(pat + 1, s + 1);
}

static int expand_globs(char **in, int in_argc, char **out, int out_max,
                        char scratch[][256], int scratch_max) {
    int outc = 0;
    int sc = 0;
    for (int i = 0; i < in_argc && outc < out_max; i++) {
        const char *arg = in[i];
        if (!has_wildcards(arg)) {
            out[outc++] = in[i];
            continue;
        }

        const char *slash = NULL;
        for (const char *p = arg; *p; p++) {
            if (*p == '/') slash = p;
        }
        char dir[256];
        char pat[128];
        int preserve_path = 0;
        if (slash) {
            size_t dlen = (size_t)(slash - arg);
            if (dlen == 0) {
                str_copy(dir, "/", sizeof(dir));
            } else {
                if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
                for (size_t k = 0; k < dlen; k++) dir[k] = arg[k];
                dir[dlen] = '\0';
            }
            str_copy(pat, slash + 1, sizeof(pat));
            preserve_path = 1;
        } else {
            str_copy(dir, ".", sizeof(dir));
            str_copy(pat, arg, sizeof(pat));
        }

        long fd = syscall3(SYS_OPEN, (long)dir, O_RDONLY | O_DIRECTORY, 0);
        if (fd < 0) {
            out[outc++] = in[i];
            continue;
        }

        int matched = 0;
        char buf[1024];
        while (1) {
            long nread = syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
            if (nread <= 0) break;
            long bpos = 0;
            while (bpos < nread && outc < out_max && sc < scratch_max) {
                struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
                if (d->d_name[0] &&
                    !(d->d_name[0] == '.' && (pat[0] != '.')) &&
                    wildcard_match(pat, d->d_name)) {
                    if (preserve_path) {
                        str_copy(scratch[sc], dir, 256);
                        size_t dl = str_len(scratch[sc]);
                        if (dl > 0 && scratch[sc][dl - 1] != '/') {
                            scratch[sc][dl++] = '/';
                            scratch[sc][dl] = '\0';
                        }
                        str_copy(scratch[sc] + dl, d->d_name, 256 - dl);
                    } else {
                        str_copy(scratch[sc], d->d_name, 256);
                    }
                    out[outc++] = scratch[sc++];
                    matched = 1;
                }
                if (d->d_reclen == 0) break;
                bpos += d->d_reclen;
            }
        }
        syscall1(SYS_CLOSE, fd);
        if (!matched && outc < out_max) {
            out[outc++] = in[i];
        }
    }
    return outc;
}

static int parse_redirections(char **args, int argc, char **cmd, int cmd_max,
                              const char **in_path, const char **out_path, int *append) {
    int cmdc = 0;
    *in_path = NULL;
    *out_path = NULL;
    *append = 0;
    for (int i = 0; i < argc; i++) {
        if (str_cmp(args[i], "<") == 0 || str_cmp(args[i], ">") == 0 || str_cmp(args[i], ">>") == 0) {
            if (i + 1 >= argc) {
                write_str("shell: redirection missing file operand\n");
                return -1;
            }
            if (str_cmp(args[i], "<") == 0) {
                *in_path = args[++i];
            } else if (str_cmp(args[i], ">>") == 0) {
                *out_path = args[++i];
                *append = 1;
            } else {
                *out_path = args[++i];
                *append = 0;
            }
            continue;
        }
        if (cmdc < cmd_max) {
            cmd[cmdc++] = args[i];
        }
    }
    return cmdc;
}

static int apply_redirections(const char *in_path, const char *out_path, int append,
                              int *saved_in, int *saved_out) {
    const int CLOSE_RESTORE = -2;
    *saved_in = -1;
    *saved_out = -1;

    if (in_path) {
        long fd = syscall3(SYS_OPEN, (long)in_path, O_RDONLY, 0);
        if (fd < 0) {
            print_errno("open", fd);
            return -1;
        }
        *saved_in = (int)syscall1(SYS_DUP, 0);
        if (*saved_in < 0) {
            *saved_in = CLOSE_RESTORE;
        }
        if (syscall2(SYS_DUP2, fd, 0) < 0) {
            syscall1(SYS_CLOSE, fd);
            write_str("shell: failed to redirect stdin\n");
            return -1;
        }
        syscall1(SYS_CLOSE, fd);
    }

    if (out_path) {
        long oflags = O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC);
        long fd = syscall3(SYS_OPEN, (long)out_path, oflags, 0644);
        if (fd < 0) {
            print_errno("open", fd);
            return -1;
        }
        *saved_out = (int)syscall1(SYS_DUP, 1);
        if (*saved_out < 0) {
            *saved_out = CLOSE_RESTORE;
        }
        if (syscall2(SYS_DUP2, fd, 1) < 0) {
            syscall1(SYS_CLOSE, fd);
            write_str("shell: failed to redirect stdout\n");
            return -1;
        }
        syscall1(SYS_CLOSE, fd);
    }

    return 0;
}

static void restore_redirections(int saved_in, int saved_out) {
    const int CLOSE_RESTORE = -2;
    if (saved_in >= 0) {
        syscall2(SYS_DUP2, saved_in, 0);
        syscall1(SYS_CLOSE, saved_in);
    } else if (saved_in == CLOSE_RESTORE) {
        syscall1(SYS_CLOSE, 0);
    }
    if (saved_out >= 0) {
        syscall2(SYS_DUP2, saved_out, 1);
        syscall1(SYS_CLOSE, saved_out);
    } else if (saved_out == CLOSE_RESTORE) {
        syscall1(SYS_CLOSE, 1);
    }
}

static int lookup_user_record(const char *username, struct user_record *out) {
    char filebuf[4096];
    int n = read_small_file("/etc/passwd", filebuf, sizeof(filebuf));
    if (n >= 0) {
        char *line = filebuf;
        while (*line) {
            while (*line == '\n' || *line == '\r') line++;
            if (!*line) break;
            char *next = line;
            while (*next && *next != '\n') next++;
            if (*next == '\n') *next++ = '\0';

            if (line[0] != '#' && line[0] != '\0') {
                char *f[7];
                int nf = split_colon_fields(line, f, 7);
                if (nf >= 5 && str_cmp(f[0], username) == 0) {
                    unsigned uid = 0, gid = 0;
                    if (parse_uint10(f[2], &uid) < 0 || parse_uint10(f[3], &gid) < 0) {
                        return -1;
                    }
                    str_copy(out->name, f[0], sizeof(out->name));
                    str_copy(out->pass, f[1], sizeof(out->pass));
                    out->uid = uid;
                    out->gid = gid;
                    if (nf >= 6) str_copy(out->home, f[5], sizeof(out->home));
                    else out->home[0] = '\0';
                    if (nf >= 7) str_copy(out->shell, f[6], sizeof(out->shell));
                    else out->shell[0] = '\0';
                    return 0;
                }
            }
            line = next;
        }
    }

    if (lookup_static_user_record(username, out) == 0) {
        return 0;
    }
    return (n < 0) ? n : -2;
}

static int lookup_uid_name(unsigned uid, char *name_out, size_t cap) {
    char filebuf[4096];
    int n = read_small_file("/etc/passwd", filebuf, sizeof(filebuf));
    if (n >= 0) {
        char *line = filebuf;
        while (*line) {
            while (*line == '\n' || *line == '\r') line++;
            if (!*line) break;
            char *next = line;
            while (*next && *next != '\n') next++;
            if (*next == '\n') *next++ = '\0';
            if (line[0] != '#' && line[0] != '\0') {
                char *f[7];
                unsigned v = 0;
                int nf = split_colon_fields(line, f, 7);
                if (nf >= 4 && parse_uint10(f[2], &v) == 0 && v == uid) {
                    str_copy(name_out, f[0], cap);
                    return 0;
                }
            }
            line = next;
        }
    }
    if (lookup_static_uid_name(uid, name_out, cap) == 0) {
        return 0;
    }
    return (n < 0) ? n : -1;
}

static int lookup_uid_record(unsigned uid, struct user_record *out) {
    char name[32];
    if (lookup_uid_name(uid, name, sizeof(name)) < 0) {
        return -1;
    }
    return lookup_user_record(name, out);
}

static void set_identity_env(const struct user_record *u) {
    char fallback_home[96];
    const char *home = u->home[0] ? u->home : NULL;
    const char *shell = u->shell[0] ? u->shell : "/bin/sh";

    if (!home) {
        if (u->uid == 0) {
            home = "/root";
        } else {
            fallback_home[0] = '\0';
            str_copy(fallback_home, "/home/", sizeof(fallback_home));
            str_append(fallback_home, u->name, sizeof(fallback_home));
            home = fallback_home;
        }
    }

    build_env_kv(env_home, sizeof(env_home), "HOME", home);
    build_env_kv(env_shell, sizeof(env_shell), "SHELL", shell);
    build_env_kv(env_user, sizeof(env_user), "USER", u->name[0] ? u->name : "unknown");
}

static int prompt_password(char *out, size_t cap) {
    write_str("Password: ");
    long n = syscall3(SYS_READ, 0, (long)out, (long)(cap - 1));
    if (n < 0) {
        write_ch('\n');
        return (int)n;
    }
    size_t len = (size_t)n;
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) len--;
    out[len] = '\0';
    return 0;
}

static int parse_hex_u64(const char *s, uint64_t *out) {
    uint64_t v = 0;
    int n = 0;
    while (s[n]) {
        char c = s[n];
        uint64_t d;
        if (c >= '0' && c <= '9') d = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint64_t)(10 + c - 'a');
        else if (c >= 'A' && c <= 'F') d = (uint64_t)(10 + c - 'A');
        else return -1;
        v = (v << 4) | d;
        n++;
    }
    if (n == 0 || n > 16) return -1;
    *out = v;
    return 0;
}

static uint64_t fnv1a64(const char *s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; s[i]; i++) {
        h ^= (uint8_t)s[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static int verify_password(const char *stored, const char *input) {
    if (!stored || stored[0] == '\0') return 1;
    if (str_cmp(stored, "x") == 0 || str_cmp(stored, "*") == 0) return 0;
    if (str_ncmp(stored, "plain$", 6) == 0) {
        return str_cmp(stored + 6, input) == 0;
    }
    if (str_ncmp(stored, "fnv1a64$", 8) == 0) {
        uint64_t hv = 0;
        if (parse_hex_u64(stored + 8, &hv) < 0) return 0;
        return fnv1a64(input) == hv;
    }
    return str_cmp(stored, input) == 0; /* legacy plain-text */
}

struct sudo_policy {
    int allowed;
    int nopasswd;
    int cmd_all;
    char cmd[128];
};

static char *trim_ws(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = str_len(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    return s;
}

static char *find_substr(char *hay, const char *needle) {
    size_t n = str_len(needle);
    if (n == 0) return hay;
    for (size_t i = 0; hay[i]; i++) {
        size_t j = 0;
        while (j < n && hay[i + j] == needle[j]) j++;
        if (j == n) return &hay[i];
    }
    return NULL;
}

static void parse_sudoers_line(char *line, const char *username, const char *cmd0,
                               struct sudo_policy *pol) {
    char *p = trim_ws(line);
    if (p[0] == '\0' || p[0] == '#') return;

    char actor[32];
    int ai = 0;
    while (p[ai] && p[ai] != ' ' && p[ai] != '\t' && ai < (int)sizeof(actor) - 1) {
        actor[ai] = p[ai];
        ai++;
    }
    actor[ai] = '\0';
    if (!(str_cmp(actor, username) == 0 || str_cmp(actor, "ALL") == 0)) {
        return;
    }

    char *rest = trim_ws(p + ai);
    if (rest[0] == '\0') return;

    int nopw = 0;
    char *np = find_substr(rest, "NOPASSWD:");
    if (np) {
        nopw = 1;
        rest = trim_ws(np + 9);
    } else {
        char *colon = NULL;
        for (char *q = rest; *q; q++) if (*q == ':') colon = q;
        if (colon) rest = trim_ws(colon + 1);
    }

    if (rest[0] == '\0') {
        /* fallback to last token */
        char *last = rest;
        for (char *q = p; *q; q++) {
            if (*q == ' ' || *q == '\t') {
                while (*q == ' ' || *q == '\t') q++;
                if (*q) last = q;
            }
        }
        rest = trim_ws(last);
    }

    int allow_cmd = 0;
    if (str_ncmp(rest, "ALL", 3) == 0 && (rest[3] == '\0' || rest[3] == ',' || rest[3] == ' ' || rest[3] == '\t')) {
        allow_cmd = 1;
    } else {
        char tmp[256];
        str_copy(tmp, rest, sizeof(tmp));
        char *it = tmp;
        while (*it) {
            while (*it == ' ' || *it == '\t' || *it == ',') it++;
            if (!*it) break;
            char *start = it;
            while (*it && *it != ',' && *it != ' ' && *it != '\t') it++;
            char hold = *it;
            *it = '\0';
            const char *base = base_name(start);
                if (str_cmp(start, cmd0) == 0 ||
                    str_cmp(base, cmd0) == 0 ||
                    str_cmp(base, base_name(cmd0)) == 0 ||
                    wildcard_match(start, cmd0) ||
                    wildcard_match(start, base_name(cmd0)) ||
                    wildcard_match(base, base_name(cmd0))) {
                allow_cmd = 1;
                *it = hold;
                break;
            }
            *it = hold;
            if (*it) it++;
        }
    }

    if (allow_cmd) {
        pol->allowed = 1;
        if (nopw) pol->nopasswd = 1;
        if (str_ncmp(rest, "ALL", 3) == 0) pol->cmd_all = 1;
    }
}

static __attribute__((unused)) struct sudo_policy sudo_lookup_policy(const char *username, const char *cmd0) {
    struct sudo_policy pol;
    pol.allowed = 0;
    pol.nopasswd = 0;
    pol.cmd_all = 0;
    pol.cmd[0] = '\0';

    long euid = syscall0(SYS_GETEUID);
    if (euid == 0) {
        pol.allowed = 1;
        pol.nopasswd = 1;
        pol.cmd_all = 1;
        return pol;
    }

    char filebuf[4096];
    int n = read_small_file("/etc/sudoers", filebuf, sizeof(filebuf));
    if (n < 0) return pol;
    char *line = filebuf;
    while (*line) {
        while (*line == '\n' || *line == '\r') line++;
        if (!*line) break;
        char *next = line;
        while (*next && *next != '\n') next++;
        if (*next == '\n') *next++ = '\0';
        parse_sudoers_line(line, username, cmd0, &pol);
        line = next;
    }
    return pol;
}

static int applet_su(int argc, char **argv) {
    const char *target = "root";
    int cmd_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (str_cmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                write_str("su: option requires an argument -- c\n");
                return 1;
            }
            cmd_idx = i + 1;
            break;
        }
        target = argv[i];
    }

    struct user_record u;
    if (lookup_user_record(target, &u) < 0) {
        write_str("su: unknown user\n");
        return 1;
    }

    long euid = syscall0(SYS_GETEUID);
    if (euid != 0 && u.pass[0]) {
        char input[64];
        if (prompt_password(input, sizeof(input)) < 0 || !verify_password(u.pass, input)) {
            write_str("su: authentication failure\n");
            return 1;
        }
    }

    long pid = syscall0(SYS_FORK);
    if (pid < 0) {
        print_errno("su: fork", pid);
        return 1;
    }
    if (pid == 0) {
        if (syscall1(SYS_SETGID, (long)u.gid) < 0 ||
            syscall1(SYS_SETUID, (long)u.uid) < 0) {
            write_str("su: credential switch failed\n");
            syscall1(SYS_EXIT, 1);
        }
        set_identity_env(&u);
        if (cmd_idx >= 0) {
            char cmdline[256];
            char token_buf[32][256];
            char *cmd_argv[32];
            int cmd_argc;
            str_copy(cmdline, argv[cmd_idx], sizeof(cmdline));
            cmd_argc = parse_words_shell(cmdline, token_buf, cmd_argv, 32, 1);
            if (cmd_argc <= 0) {
                syscall1(SYS_EXIT, 0);
            }
            cmd_argv[cmd_argc] = NULL;
            if (is_builtin_applet_name(cmd_argv[0])) {
                int rc = applet_main(cmd_argv[0], cmd_argc, cmd_argv);
                syscall1(SYS_EXIT, rc);
            }
            int erc = try_exec_external(cmd_argv);
            if (erc < 0) {
                report_exec_failure(cmd_argv[0], erc);
                syscall1(SYS_EXIT, (erc == -2) ? 127 : 1);
            }
            __builtin_unreachable();
        }
        const char *prompt = (u.uid == 0) ? "# " : "$ ";
        int rc = run_shell(prompt);
        syscall1(SYS_EXIT, rc);
    }

    int status = 0;
    while (syscall4(SYS_WAIT4, pid, (long)&status, 0, 0) < 0) {
    }
    return 0;
}

static int applet_sudo(int argc, char **argv) {
    if (argc < 2) {
        write_str("usage: sudo <command> [args...]\n");
        return 1;
    }

    if (syscall0(SYS_GETEUID) != 0) {
        write_str("sudo: minimal mode active, only root may run sudo\n");
        return 1;
    }

    long pid = syscall0(SYS_FORK);
    if (pid < 0) {
        print_errno("sudo: fork", pid);
        return 1;
    }
    if (pid == 0) {
        struct user_record root_user;
        if (lookup_user_record("root", &root_user) == 0) {
            set_identity_env(&root_user);
        }
        syscall1(SYS_SETGID, 0);
        syscall1(SYS_SETUID, 0);
        {
            const char *cmd_name = base_name(argv[1]);
            if (is_builtin_applet_name(cmd_name)) {
                int rc = applet_main(cmd_name, argc - 1, &argv[1]);
                syscall1(SYS_EXIT, rc);
            }
        }
        int erc = try_exec_external(&argv[1]);
        if (erc < 0) {
            report_exec_failure(argv[1], erc);
            syscall1(SYS_EXIT, (erc == -2) ? 127 : 1);
        }
        __builtin_unreachable();
    }

    int status = 0;
    while (syscall4(SYS_WAIT4, pid, (long)&status, 0, 0) < 0) {
    }
    return 0;
}

static const char *builtin_names[] = {
    "help", "echo", "uname", "clear", "reboot", "shutdown", "exit",
    "zsh", "sh", "busybox", "cd", "pwd", "ls", "cat", "touch",
    "mkdir", "rm", "rmdir", "write", "chmod", "chown", "stat",
    "su", "sudo", "whoami", "id",
    "sysctl", "installer", "installer-tui", NULL
};

static int add_candidate(char out[][128], int count, int max, const char *name) {
    for (int i = 0; i < count; i++) {
        if (str_cmp(out[i], name) == 0) return count;
    }
    if (count < max) {
        str_copy(out[count], name, 128);
        return count + 1;
    }
    return count;
}

static int collect_matches_from_dir(const char *dir, const char *prefix,
                                    char out[][128], int count, int max) {
    long fd = syscall3(SYS_OPEN, (long)dir, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return count;
    size_t plen = str_len(prefix);
    char buf[1024];
    while (1) {
        long nread = syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
        if (nread <= 0) break;
        long bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            if (d->d_name[0] && d->d_name[0] != '.') {
                if (str_ncmp(d->d_name, prefix, plen) == 0) {
                    count = add_candidate(out, count, max, d->d_name);
                }
            }
            if (d->d_reclen == 0) break;
            bpos += d->d_reclen;
        }
    }
    syscall1(SYS_CLOSE, fd);
    return count;
}

static int str_has_char(const char *s, char ch) {
    while (*s) {
        if (*s == ch) return 1;
        s++;
    }
    return 0;
}

static int common_prefix_len(char vals[][128], int n) {
    if (n <= 0) return 0;
    int len = (int)str_len(vals[0]);
    for (int i = 1; i < n; i++) {
        int j = 0;
        int ilen = (int)str_len(vals[i]);
        int lim = (ilen < len) ? ilen : len;
        while (j < lim && vals[0][j] == vals[i][j]) j++;
        len = j;
        if (len == 0) break;
    }
    return len;
}

static int replace_span(char *line, size_t cap, size_t start, size_t end,
                        const char *repl, size_t *cursor) {
    size_t len = str_len(line);
    size_t rlen = str_len(repl);
    size_t tail = len - end;
    if (start > end || end > len) {
        return -1;
    }
    if (start + rlen + tail + 1 > cap) {
        return -1;
    }
    for (size_t i = 0; i <= tail; i++) {
        line[start + rlen + i] = line[end + i];
    }
    for (size_t i = 0; i < rlen; i++) {
        line[start + i] = repl[i];
    }
    *cursor = start + rlen;
    return 0;
}

static int collect_path_matches(const char *token, char out[][128], int count, int max) {
    char dir[256];
    char base[128];
    char candidate[128];
    const char *slash = NULL;
    int has_slash = 0;

    for (const char *p = token; *p; p++) {
        if (*p == '/') slash = p;
    }
    if (slash) {
        has_slash = 1;
        size_t dlen = (size_t)(slash - token);
        if (dlen == 0) {
            str_copy(dir, "/", sizeof(dir));
        } else {
            if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
            for (size_t i = 0; i < dlen; i++) dir[i] = token[i];
            dir[dlen] = '\0';
        }
        str_copy(base, slash + 1, sizeof(base));
    } else {
        str_copy(dir, ".", sizeof(dir));
        str_copy(base, token, sizeof(base));
    }

    long fd = syscall3(SYS_OPEN, (long)dir, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return count;
    size_t plen = str_len(base);
    char buf[1024];
    while (1) {
        long nread = syscall3(SYS_GETDENTS64, fd, (long)buf, sizeof(buf));
        if (nread <= 0) break;
        long bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            if (d->d_name[0] &&
                !(d->d_name[0] == '.' && base[0] != '.') &&
                str_ncmp(d->d_name, base, plen) == 0) {
                if (has_slash) {
                    if (str_cmp(dir, "/") == 0) {
                        str_copy(candidate, "/", sizeof(candidate));
                        str_append(candidate, d->d_name, sizeof(candidate));
                    } else {
                        str_copy(candidate, dir, sizeof(candidate));
                        str_append(candidate, "/", sizeof(candidate));
                        str_append(candidate, d->d_name, sizeof(candidate));
                    }
                } else {
                    str_copy(candidate, d->d_name, sizeof(candidate));
                }
                count = add_candidate(out, count, max, candidate);
            }
            if (d->d_reclen == 0) break;
            bpos += d->d_reclen;
        }
    }
    syscall1(SYS_CLOSE, fd);
    return count;
}

static int complete_at_cursor(char *line, size_t *cursor, size_t cap) {
    char token[128];
    char matches[64][128];
    char repl[128];
    int mcount = 0;
    size_t len = str_len(line);
    size_t cur = *cursor;
    size_t start;
    size_t tlen;

    if (cur > len) cur = len;
    start = cur;
    while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t') {
        start--;
    }
    tlen = cur - start;
    if (tlen == 0 || tlen >= sizeof(token)) {
        return 0;
    }
    for (size_t i = 0; i < tlen; i++) {
        token[i] = line[start + i];
    }
    token[tlen] = '\0';

    if (start == 0 && !str_has_char(token, '/')) {
        for (int i = 0; builtin_names[i]; i++) {
            if (str_ncmp(builtin_names[i], token, tlen) == 0) {
                mcount = add_candidate(matches, mcount, 64, builtin_names[i]);
            }
        }
        mcount = collect_matches_from_dir("/bin", token, matches, mcount, 64);
        mcount = collect_matches_from_dir("/sbin", token, matches, mcount, 64);
        mcount = collect_matches_from_dir("/usr/bin", token, matches, mcount, 64);
    } else {
        mcount = collect_path_matches(token, matches, 0, 64);
    }

    if (mcount == 0) {
        return 0;
    }

    if (mcount == 1) {
        str_copy(repl, matches[0], sizeof(repl));
        str_append(repl, " ", sizeof(repl));
        if (replace_span(line, cap, start, cur, repl, cursor) == 0) {
            return 1;
        }
        return 0;
    }

    int cplen = common_prefix_len(matches, mcount);
    if (cplen > (int)tlen) {
        char cpbuf[128];
        for (int i = 0; i < cplen && i < (int)sizeof(cpbuf) - 1; i++) {
            cpbuf[i] = matches[0][i];
        }
        cpbuf[(cplen < (int)sizeof(cpbuf)) ? cplen : (int)sizeof(cpbuf) - 1] = '\0';
        if (replace_span(line, cap, start, cur, cpbuf, cursor) == 0) {
            return 1;
        }
        return 0;
    }

    for (int i = 0; i < mcount; i++) {
        write_str(matches[i]);
        write_str("  ");
    }
    write_ch('\n');
    return 2;
}

static void redraw_line(const char *prompt, const char *line, size_t cursor) {
    size_t len = str_len(line);
    write_ch('\r');
    write_str(prompt);
    write_str(line);
    write_str("\033[K");
    while (len > cursor) {
        write_ch('\b');
        len--;
    }
}

static int read_line_interactive(const char *prompt, char *line, size_t cap,
                                 char history[][256], int hist_count, int *hist_pos) {
    size_t len = 0;
    size_t cursor = 0;
    line[0] = '\0';
    write_str(prompt);

    while (1) {
        char c = 0;
        long n = syscall3(SYS_READ, 0, (long)&c, 1);
        if (n == -4) { /* EINTR */
            line[0] = '\0';
            len = 0;
            cursor = 0;
            /* Make Ctrl-C recovery visually clean. */
            write_ch('\n');
            write_str(prompt);
            continue;
        }
        if (n <= 0) {
            continue;
        }

        if (c == '\n') {
            write_ch('\n');
            line[len] = '\0';
            return 0;
        }

        if (c == '\t') {
            int comp = complete_at_cursor(line, &cursor, cap);
            if (comp == 1 || comp == 2) {
                len = str_len(line);
                redraw_line(prompt, line, cursor);
            }
            continue;
        }

        if (c == 0x1b) {
            char a = 0, b = 0;
            if (syscall3(SYS_READ, 0, (long)&a, 1) <= 0) continue;
            if (syscall3(SYS_READ, 0, (long)&b, 1) <= 0) continue;
            if (a == '[') {
                if (b == 'A') { /* Up */
                    if (hist_count > 0) {
                        if (*hist_pos < 0) {
                            *hist_pos = hist_count - 1;
                        } else if (*hist_pos > 0) {
                            (*hist_pos)--;
                        }
                        str_copy(line, history[*hist_pos], cap);
                        len = str_len(line);
                        cursor = len;
                        redraw_line(prompt, line, cursor);
                    }
                    continue;
                }
                if (b == 'B') { /* Down -> clear line */
                    if (*hist_pos >= 0 && *hist_pos < hist_count - 1) {
                        (*hist_pos)++;
                        str_copy(line, history[*hist_pos], cap);
                        len = str_len(line);
                    } else {
                        *hist_pos = -1;
                        line[0] = '\0';
                        len = 0;
                    }
                    cursor = len;
                    redraw_line(prompt, line, cursor);
                    continue;
                }
                if (b == 'C') { /* Right */
                    if (cursor < len) {
                        write_ch(line[cursor]);
                        cursor++;
                    }
                    continue;
                }
                if (b == 'D') { /* Left */
                    if (cursor > 0) {
                        write_ch('\b');
                        cursor--;
                    }
                    continue;
                }
                if (b == '3') { /* Delete key: ESC [ 3 ~ */
                    char t = 0;
                    if (syscall3(SYS_READ, 0, (long)&t, 1) > 0 && t == '~') {
                        if (cursor < len) {
                            for (size_t i = cursor; i < len; i++) {
                                line[i] = line[i + 1];
                            }
                            len--;
                            redraw_line(prompt, line, cursor);
                        }
                    }
                    continue;
                }
            }
            continue;
        }

        if (c == '\b' || c == 0x7f) {
            if (cursor > 0) {
                for (size_t i = cursor - 1; i < len; i++) {
                    line[i] = line[i + 1];
                }
                cursor--;
                len--;
                redraw_line(prompt, line, cursor);
            }
            continue;
        }

        if (c >= 32 && c <= 126) {
            if (len + 1 < cap) {
                if (cursor == len) {
                    line[len++] = c;
                    line[len] = '\0';
                    cursor = len;
                    write_ch(c);
                } else {
                    for (size_t i = len + 1; i > cursor; i--) {
                        line[i] = line[i - 1];
                    }
                    line[cursor++] = c;
                    len++;
                    redraw_line(prompt, line, cursor);
                }
            }
            continue;
        }
    }
}

static int exec_with_sh_fallback(const char *path, char **args) {
    long ret = syscall3(SYS_EXECVE, (long)path, (long)args, (long)shell_envp);
    if (ret != -8) { /* ENOEXEC */
        return (int)ret;
    }

    char *sh_argv[20];
    int argc = 0;
    while (args[argc] && argc < 18) {
        argc++;
    }
    int k = 0;
    sh_argv[k++] = "/bin/sh";
    sh_argv[k++] = (char *)path;
    for (int i = 1; i < argc; i++) {
        sh_argv[k++] = args[i];
    }
    sh_argv[k] = NULL;
    return (int)syscall3(SYS_EXECVE, (long)"/bin/sh", (long)sh_argv, (long)shell_envp);
}

static void report_exec_failure(const char *cmd, int ret) {
    if (ret == -2) { /* ENOENT */
        write_str(cmd);
        write_str(": command not found\n");
    } else {
        print_errno(cmd, ret);
    }
}

static int try_exec_external(char **args) {
    if (!args || !args[0] || args[0][0] == '\0') return -1;

    if (args[0][0] == '/') {
        return exec_with_sh_fallback(args[0], args);
    }
    if (args[0][0] == '.' && args[0][1] == '/') {
        char cwd[256];
        char abs[512];
        long ret = syscall2(SYS_GETCWD, (long)cwd, sizeof(cwd));
        if (ret >= 0) {
            size_t clen = str_len(cwd);
            size_t nlen = str_len(args[0] + 2);
            if (clen + 1 + nlen + 1 < sizeof(abs)) {
                str_copy(abs, cwd, sizeof(abs));
                if (clen > 0 && abs[clen - 1] != '/') {
                    abs[clen++] = '/';
                    abs[clen] = '\0';
                }
                str_copy(abs + clen, args[0] + 2, sizeof(abs) - clen);
                return exec_with_sh_fallback(abs, args);
            }
        }
        return exec_with_sh_fallback(args[0], args);
    }

    static const char *paths[] = { "/bin", "/sbin", "/usr/bin" };
    int last_err = -2; /* ENOENT by default */
    char candidate[256];
    for (int i = 0; i < 3; i++) {
        size_t dlen = str_len(paths[i]);
        size_t nlen = str_len(args[0]);
        if (dlen + 1 + nlen + 1 >= sizeof(candidate)) continue;
        str_copy(candidate, paths[i], sizeof(candidate));
        candidate[dlen] = '/';
        str_copy(candidate + dlen + 1, args[0], sizeof(candidate) - dlen - 1);
        long ret = exec_with_sh_fallback(candidate, args);
        if (ret >= 0) return 0;
        if (ret != -2) {
            last_err = (int)ret;
        }
    }
    return last_err;
}

static int run_external(char **args) {
    long pid = syscall0(SYS_FORK);
    if (pid < 0) {
        print_errno("fork", pid);
        return -1;
    }
    if (pid == 0) {
        int erc = try_exec_external(args);
        if (erc < 0) {
            report_exec_failure(args[0], erc);
            syscall1(SYS_EXIT, (erc == -2) ? 127 : 1);
        }
        __builtin_unreachable();
    }

    int status = 0;
    while (1) {
        long ret = syscall4(SYS_WAIT4, pid, (long)&status, 0, 0);
        if (ret >= 0) {
            break;
        }
        if (ret == -4) { /* EINTR */
            continue;
        }
        print_errno("wait4", ret);
        break;
    }
    return (status == 0) ? 0 : 1;
}

static int find_unquoted_pipe_pos(const char *s) {
    int in_single = 0, in_double = 0, esc = 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (esc) {
            esc = 0;
            continue;
        }
        if (c == '\\') {
            esc = 1;
            continue;
        }
        if (!in_double && c == '\'') {
            in_single = !in_single;
            continue;
        }
        if (!in_single && c == '"') {
            in_double = !in_double;
            continue;
        }
        if (!in_single && !in_double && c == '|') {
            if (s[i + 1] == '|') {
                i++;
                continue;
            }
            return i;
        }
    }
    return -1;
}

static int split_pipeline_segments(char *line, char segs[][256], int max_segs) {
    int in_single = 0, in_double = 0, esc = 0;
    int segc = 0;
    int si = 0;
    if (max_segs <= 0) return 0;
    segs[0][0] = '\0';
    segc = 1;
    for (int i = 0; line[i]; i++) {
        char c = line[i];
        if (esc) {
            if (si < 255) segs[segc - 1][si++] = c;
            esc = 0;
            continue;
        }
        if (c == '\\') {
            if (si < 255) segs[segc - 1][si++] = c;
            esc = 1;
            continue;
        }
        if (!in_double && c == '\'') {
            in_single = !in_single;
            if (si < 255) segs[segc - 1][si++] = c;
            continue;
        }
        if (!in_single && c == '"') {
            in_double = !in_double;
            if (si < 255) segs[segc - 1][si++] = c;
            continue;
        }
        if (!in_single && !in_double && c == '|') {
            if (line[i + 1] == '|') {
                if (si < 255) segs[segc - 1][si++] = c;
                continue;
            }
            segs[segc - 1][si] = '\0';
            if (segc >= max_segs) {
                return -1;
            }
            segs[segc][0] = '\0';
            segc++;
            si = 0;
            continue;
        }
        if (si < 255) segs[segc - 1][si++] = c;
    }
    segs[segc - 1][si] = '\0';
    return segc;
}

static int execute_pipeline_line(const char *prompt, char *line) {
    (void)prompt;
    char segs[8][256];
    int segc = split_pipeline_segments(line, segs, 8);
    if (segc < 2) {
        return -1;
    }
    if (segc < 0) {
        write_str("shell: pipeline too long\n");
        return 1;
    }

    int pipes[7][2];
    long pids[8];
    for (int i = 0; i < 7; i++) { pipes[i][0] = -1; pipes[i][1] = -1; }
    for (int i = 0; i < 8; i++) { pids[i] = -1; }

    for (int i = 0; i < segc - 1; i++) {
        long prc = syscall1(SYS_PIPE, (long)pipes[i]);
        if (prc < 0) {
            print_errno("pipe", prc);
            return 1;
        }
    }

    for (int i = 0; i < segc; i++) {
        char *trimmed = trim_ws_inplace(segs[i]);
        if (*trimmed == '\0') {
            write_str("shell: empty pipeline segment\n");
            return 1;
        }

        long pid = syscall0(SYS_FORK);
        if (pid < 0) {
            print_errno("fork", pid);
            return 1;
        }
        if (pid == 0) {
            if (i > 0) {
                syscall2(SYS_DUP2, pipes[i - 1][0], 0);
            }
            if (i + 1 < segc) {
                syscall2(SYS_DUP2, pipes[i][1], 1);
            }
            for (int j = 0; j < segc - 1; j++) {
                if (pipes[j][0] >= 0) syscall1(SYS_CLOSE, pipes[j][0]);
                if (pipes[j][1] >= 0) syscall1(SYS_CLOSE, pipes[j][1]);
            }

            char *args[32];
            char *expanded[32];
            static char glob_scratch[32][256];
            char token_buf[32][256];
            int argc = parse_words_shell(trimmed, token_buf, args, 32, 1);
            argc = expand_globs(args, argc, expanded, 32, glob_scratch, 32);
            for (int k = 0; k < argc; k++) args[k] = expanded[k];
            if (argc <= 0) {
                syscall1(SYS_EXIT, 0);
            }
            args[argc] = NULL;

            for (int k = 0; k < argc; k++) {
                if (str_cmp(args[k], "<") == 0 || str_cmp(args[k], ">") == 0 || str_cmp(args[k], ">>") == 0) {
                    write_str("shell: redirects inside pipeline not supported yet\n");
                    syscall1(SYS_EXIT, 1);
                }
            }

            if (is_builtin_applet_name(args[0])) {
                int rc = applet_main(args[0], argc, args);
                syscall1(SYS_EXIT, rc);
            }
            int erc = try_exec_external(args);
            if (erc < 0) {
                report_exec_failure(args[0], erc);
                syscall1(SYS_EXIT, (erc == -2) ? 127 : 1);
            }
            __builtin_unreachable();
        }
        pids[i] = pid;
    }

    for (int i = 0; i < segc - 1; i++) {
        if (pipes[i][0] >= 0) syscall1(SYS_CLOSE, pipes[i][0]);
        if (pipes[i][1] >= 0) syscall1(SYS_CLOSE, pipes[i][1]);
    }

    int last_status = 0;
    for (int i = 0; i < segc; i++) {
        int status = 0;
        while (1) {
            long ret = syscall4(SYS_WAIT4, pids[i], (long)&status, 0, 0);
            if (ret >= 0) break;
            if (ret == -4) continue;
            break;
        }
        if (i == segc - 1) {
            last_status = status;
        }
    }
    return (last_status == 0) ? 0 : 1;
}

static char *trim_ws_inplace(char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    size_t n = str_len(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
    return s;
}

enum chain_op {
    CHAIN_NONE = 0,
    CHAIN_SEQ,
    CHAIN_AND,
    CHAIN_OR
};

static enum chain_op parse_next_segment(const char **cursor, char *out, size_t cap) {
    const char *p = *cursor;
    const char *start = p;
    size_t n = 0;
    int in_single = 0;
    int in_double = 0;

    while (*p && n + 1 < cap) {
        char c = *p;
        if (!in_single && c == '"' ) {
            in_double = !in_double;
            out[n++] = *p++;
            continue;
        }
        if (!in_double && c == '\'') {
            in_single = !in_single;
            out[n++] = *p++;
            continue;
        }
        if (!in_single && c == '\\' && p[1]) {
            out[n++] = *p++;
            if (n + 1 < cap) {
                out[n++] = *p++;
            }
            continue;
        }
        if (!in_single && !in_double) {
            if (c == ';') {
                p++;
                break;
            }
            if (c == '&' && p[1] == '&') {
                p += 2;
                out[n] = '\0';
                *cursor = p;
                return CHAIN_AND;
            }
            if (c == '|' && p[1] == '|') {
                p += 2;
                out[n] = '\0';
                *cursor = p;
                return CHAIN_OR;
            }
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
    *cursor = p;
    if (p > start && *(p - 1) == ';') {
        return CHAIN_SEQ;
    }
    return CHAIN_NONE;
}

static int execute_simple_command(const char *prompt, char *line) {
    if (find_unquoted_pipe_pos(line) >= 0) {
        return execute_pipeline_line(prompt, line);
    }

    char *args[32];
    char *cmd[32];
    char *expanded[32];
    char token_buf[32][256];
    static char glob_scratch[32][256];
    int argc = parse_words_shell(line, token_buf, args, 32, 1);
    argc = expand_globs(args, argc, expanded, 32, glob_scratch, 32);
    for (int i = 0; i < argc; i++) args[i] = expanded[i];
    if (argc == 0) {
        return 0;
    }
    const char *in_path = NULL;
    const char *out_path = NULL;
    int append = 0;
    int cmdc = parse_redirections(args, argc, cmd, 31, &in_path, &out_path, &append);
    if (cmdc < 0) {
        return 1;
    }
    if (cmdc == 0) {
        return 0;
    }
    cmd[cmdc] = NULL;
    argc = cmdc;
    for (int i = 0; i < argc; i++) args[i] = cmd[i];
    args[argc] = NULL;

    int saved_in = -1, saved_out = -1;
    int rc = 0;
    if (apply_redirections(in_path, out_path, append, &saved_in, &saved_out) < 0) {
        restore_redirections(saved_in, saved_out);
        return 1;
    }

    if (str_cmp(args[0], "help") == 0) {
        write_str("builtins: help echo uname clear reboot shutdown exit zsh sh busybox cd pwd ls cat touch mkdir rm rmdir write chmod chown stat head tail wc cut find time true false users su sudo whoami id opkg\n");
        write_str("shell: quotes, $VAR expansion, && || ;, minimal pipes |, redirects < > >>, Up/Down history, TAB completion\n");
        rc = 0;
    } else if (str_cmp(args[0], "echo") == 0) {
        applet_echo(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "uname") == 0) {
        applet_uname(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "cd") == 0) {
        applet_cd(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "pwd") == 0) {
        applet_pwd();
        rc = 0;
    } else if (str_cmp(args[0], "ls") == 0) {
        applet_ls(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "cat") == 0) {
        applet_cat(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "touch") == 0) {
        applet_touch(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "mkdir") == 0) {
        applet_mkdir(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "rm") == 0) {
        applet_rm(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "rmdir") == 0) {
        applet_rmdir(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "write") == 0) {
        applet_write(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "chmod") == 0) {
        applet_chmod(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "chown") == 0) {
        applet_chown(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "stat") == 0) {
        applet_stat(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "head") == 0) {
        applet_head(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "tail") == 0) {
        applet_tail(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "wc") == 0) {
        applet_wc(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "cut") == 0) {
        applet_cut(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "users") == 0) {
        applet_users();
        rc = 0;
    } else if (str_cmp(args[0], "find") == 0) {
        applet_find(argc, args);
        rc = 0;
    } else if (str_cmp(args[0], "time") == 0) {
        rc = applet_time_exec(argc, args);
    } else if (str_cmp(args[0], "true") == 0) {
        rc = 0;
    } else if (str_cmp(args[0], "false") == 0) {
        rc = 1;
    } else if (str_cmp(args[0], "su") == 0) {
        rc = applet_su(argc, args);
    } else if (str_cmp(args[0], "sudo") == 0) {
        rc = applet_sudo(argc, args);
    } else if (str_cmp(args[0], "whoami") == 0) {
        applet_whoami();
        rc = 0;
    } else if (str_cmp(args[0], "id") == 0) {
        applet_id();
        rc = 0;
    } else if (str_cmp(args[0], "reboot") == 0) {
        applet_reboot();
        rc = 0;
    } else if (str_cmp(args[0], "shutdown") == 0 ||
               str_cmp(args[0], "poweroff") == 0 ||
               str_cmp(args[0], "halt") == 0) {
        applet_shutdown();
        rc = 0;
    } else if (str_cmp(args[0], "clear") == 0) {
        write_str("\033[2J\033[H");
        rc = 0;
    } else if (str_cmp(args[0], "zsh") == 0) {
        rc = run_shell("zsh% ");
    } else if (str_cmp(args[0], "sh") == 0 || str_cmp(args[0], "busybox") == 0) {
        rc = run_shell("$ ");
    } else if (str_cmp(args[0], "exit") == 0) {
        restore_redirections(saved_in, saved_out);
        return -1000;
    } else {
        rc = run_external(args);
    }
    (void)prompt;
    restore_redirections(saved_in, saved_out);
    return rc;
}

static int run_shell(const char *prompt) {
    char line[256];
    char line_orig[256];
    char seg[256];
    static char history[16][256];
    int hist_count = 0;
    int hist_pos = -1;

    write_str("Obelisk shell ready. Type 'help' for commands.\n");
    while (1) {
        hist_pos = -1;
        read_line_interactive(prompt, line, sizeof(line), history, hist_count, &hist_pos);
        str_copy(line_orig, line, sizeof(line_orig));
        if (trim_ws_inplace(line_orig)[0] == '\0') {
            continue;
        }

        if (hist_count == 0 || str_cmp(history[hist_count - 1], line_orig) != 0) {
            if (hist_count < 16) {
                str_copy(history[hist_count++], line_orig, sizeof(history[0]));
            } else {
                for (int i = 1; i < 16; i++) {
                    str_copy(history[i - 1], history[i], sizeof(history[0]));
                }
                str_copy(history[15], line_orig, sizeof(history[0]));
            }
        }

        const char *cursor = line_orig;
        enum chain_op prev = CHAIN_SEQ;
        int last_status = 0;
        while (1) {
            enum chain_op next = parse_next_segment(&cursor, seg, sizeof(seg));
            char *trimmed = trim_ws_inplace(seg);
            int should_run = 1;
            if (prev == CHAIN_AND) {
                should_run = (last_status == 0);
            } else if (prev == CHAIN_OR) {
                should_run = (last_status != 0);
            }
            if (*trimmed && should_run) {
                int rc = execute_simple_command(prompt, trimmed);
                if (rc == -1000) {
                    return 0;
                }
                last_status = rc;
            }
            prev = next;
            if (next == CHAIN_NONE) {
                break;
            }
        }
    }
}

static int applet_main(const char *name, int argc, char **argv) {
    if (str_cmp(name, "busybox") == 0) {
        if (argc >= 2) {
            return applet_main(argv[1], argc - 1, &argv[1]);
        }
        return run_shell("$ ");
    }
    if (str_cmp(name, "sh") == 0 || str_cmp(name, "ash") == 0) {
        return run_shell("$ ");
    }
    if (str_cmp(name, "zsh") == 0) {
        return run_shell("zsh% ");
    }
    if (str_cmp(name, "echo") == 0) {
        applet_echo(argc, argv);
        return 0;
    }
    if (str_cmp(name, "uname") == 0) {
        applet_uname(argc, argv);
        return 0;
    }
    if (str_cmp(name, "pwd") == 0) {
        applet_pwd();
        return 0;
    }
    if (str_cmp(name, "ls") == 0) {
        applet_ls(argc, argv);
        return 0;
    }
    if (str_cmp(name, "cat") == 0) {
        applet_cat(argc, argv);
        return 0;
    }
    if (str_cmp(name, "touch") == 0) {
        applet_touch(argc, argv);
        return 0;
    }
    if (str_cmp(name, "mkdir") == 0) {
        applet_mkdir(argc, argv);
        return 0;
    }
    if (str_cmp(name, "rm") == 0) {
        applet_rm(argc, argv);
        return 0;
    }
    if (str_cmp(name, "rmdir") == 0) {
        applet_rmdir(argc, argv);
        return 0;
    }
    if (str_cmp(name, "chmod") == 0) {
        applet_chmod(argc, argv);
        return 0;
    }
    if (str_cmp(name, "chown") == 0) {
        applet_chown(argc, argv);
        return 0;
    }
    if (str_cmp(name, "stat") == 0) {
        applet_stat(argc, argv);
        return 0;
    }
    if (str_cmp(name, "head") == 0) {
        applet_head(argc, argv);
        return 0;
    }
    if (str_cmp(name, "tail") == 0) {
        applet_tail(argc, argv);
        return 0;
    }
    if (str_cmp(name, "wc") == 0) {
        applet_wc(argc, argv);
        return 0;
    }
    if (str_cmp(name, "cut") == 0) {
        applet_cut(argc, argv);
        return 0;
    }
    if (str_cmp(name, "users") == 0) {
        applet_users();
        return 0;
    }
    if (str_cmp(name, "find") == 0) {
        applet_find(argc, argv);
        return 0;
    }
    if (str_cmp(name, "time") == 0) {
        return applet_time_exec(argc, argv);
    }
    if (str_cmp(name, "true") == 0) {
        return 0;
    }
    if (str_cmp(name, "false") == 0) {
        return 1;
    }
    if (str_cmp(name, "reboot") == 0) {
        applet_reboot();
        return 0;
    }
    if (str_cmp(name, "shutdown") == 0 ||
        str_cmp(name, "poweroff") == 0 ||
        str_cmp(name, "halt") == 0) {
        applet_shutdown();
        return 0;
    }
    if (str_cmp(name, "clear") == 0) {
        write_str("\033[2J\033[H");
        return 0;
    }
    if (str_cmp(name, "su") == 0) {
        return applet_su(argc, argv);
    }
    if (str_cmp(name, "sudo") == 0) {
        return applet_sudo(argc, argv);
    }
    if (str_cmp(name, "whoami") == 0) {
        applet_whoami();
        return 0;
    }
    if (str_cmp(name, "id") == 0) {
        applet_id();
        return 0;
    }

    write_str("busybox: unsupported applet: ");
    write_str(name);
    write_str("\n");
    return 1;
}

void _start(void) {
    uint64_t *stack_ptr;
    __asm__ volatile("movq %%rsp, %0" : "=r"(stack_ptr));

    int argc = (int)stack_ptr[0];
    char **argv = (char **)&stack_ptr[1];
    char *fallback_argv[] = { "busybox", NULL };
    char **use_argv = argv;
    int use_argc = argc;

    if (use_argc <= 0 || !use_argv || !use_argv[0]) {
        /* Kernel bring-up path may not provide full argv yet. */
        use_argv = fallback_argv;
        use_argc = 1;
    }

    const char *name = base_name(use_argv[0]);
    const char *exec_name = NULL;
    {
        char **envp = &use_argv[use_argc + 1];
        while (*envp) {
            envp++;
        }
        uint64_t *auxv = (uint64_t *)(envp + 1);
        while (auxv[0] != AT_NULL) {
            if (auxv[0] == AT_EXECFN) {
                const char *execfn = (const char *)(uintptr_t)auxv[1];
                if (execfn && *execfn) {
                    exec_name = base_name(execfn);
                }
                break;
            }
            auxv += 2;
        }
    }

    /* Prefer execfn applet name for copied busybox applets.
     * This avoids falling back to shell when argv[0] is ambiguous. */
    if (exec_name && *exec_name) {
        if ((use_argc == 1 && str_cmp(exec_name, "busybox") != 0) ||
            str_cmp(name, "busybox") == 0 ||
            str_cmp(name, "sh") == 0 ||
            str_cmp(name, "zsh") == 0) {
            name = exec_name;
        }
    }

    {
        struct user_record self_user;
        long uid = syscall0(SYS_GETUID);
        if (uid >= 0 && lookup_uid_record((unsigned)uid, &self_user) == 0) {
            set_identity_env(&self_user);
        }
    }

    int rc = applet_main(name, use_argc, use_argv);
    syscall1(SYS_EXIT, rc);
    __builtin_unreachable();
}
