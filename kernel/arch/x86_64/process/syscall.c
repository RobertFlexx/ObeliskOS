/*
 * Obelisk OS - System Call Handler
 * From Axioms, Order.
 *
 * Implements the system call dispatch mechanism.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <obelisk/errno.h>
#include <arch/cpu.h>
#include <arch/regs.h>
#include <uapi/syscall.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <fs/file.h>
#include <mm/pmm.h>
#include <obelisk/limits.h>

/* Forward declarations of syscall handlers */
static int64_t sys_read(int fd, void *buf, size_t count);
static int64_t sys_write(int fd, const void *buf, size_t count);
static int64_t sys_writev(int fd, const void *iov, int iovcnt);
static int64_t sys_open(const char *pathname, int flags, mode_t mode);
static int64_t sys_close(int fd);
static int64_t sys_stat(const char *pathname, struct stat *statbuf);
static int64_t sys_fstat(int fd, struct stat *statbuf);
static int64_t sys_access(const char *pathname, int mode);
static int64_t sys_lseek(int fd, off_t offset, int whence);
static int64_t sys_mprotect(void *addr, size_t len, int prot);
static int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
static int64_t sys_munmap(void *addr, size_t length);
static int64_t sys_brk(void *addr);
static int64_t sys_sched_yield(void);
static int64_t sys_fork(void);
static int64_t sys_execve(const char *pathname, char *const argv[], char *const envp[]);
static int64_t sys_exit(int status);
static int64_t sys_exit_group(int status);
static int64_t sys_wait4(pid_t pid, int *wstatus, int options, void *rusage);
static int64_t sys_getpid(void);
static int64_t sys_gettid(void);
static int64_t sys_getppid(void);
static int64_t sys_getuid(void);
static int64_t sys_getgid(void);
static int64_t sys_geteuid(void);
static int64_t sys_getegid(void);
static int64_t sys_setuid(uid_t uid);
static int64_t sys_setgid(gid_t gid);
static int64_t sys_setreuid(uid_t ruid, uid_t euid);
static int64_t sys_setregid(gid_t rgid, gid_t egid);
static int64_t sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);
static int64_t sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);
static int64_t sys_setfsuid(uid_t fsuid);
static int64_t sys_setfsgid(gid_t fsgid);
static int64_t sys_chdir(const char *path);
static int64_t sys_getcwd(char *buf, size_t size);
static int64_t sys_mkdir(const char *pathname, mode_t mode);
static int64_t sys_rmdir(const char *pathname);
static int64_t sys_unlink(const char *pathname);
static int64_t sys_link(const char *oldpath, const char *newpath);
static int64_t sys_rename(const char *oldpath, const char *newpath);
static int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode);
static int64_t sys_newfstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags);
static int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
static int64_t sys_faccessat(int dirfd, const char *pathname, int mode);
static int64_t sys_dup(int oldfd);
static int64_t sys_dup2(int oldfd, int newfd);
static int64_t sys_pipe(int pipefd[2]);
static int64_t sys_ioctl(int fd, unsigned long request, void *arg);
static int64_t sys_fcntl(int fd, int cmd, uint64_t arg);
static int64_t sys_getdents64(int fd, void *dirp, size_t count);
static int64_t sys_kill(pid_t pid, int sig);
static int64_t sys_uname(void *buf);
static int64_t sys_reboot(int magic1, int magic2, unsigned int cmd, void *arg);
static int64_t sys_sysctl(void *args);

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
} __packed;

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

/* Minimal Linux-compatible tty ioctl data. */
struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

struct termios_compat {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[32];
};

#define TCGETS      0x5401UL
#define TCSETS      0x5402UL
#define TCSETSW     0x5403UL
#define TCSETSF     0x5404UL
#define TIOCGWINSZ  0x5413UL

#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4

#define IS_STDIO_FD(fd) ((fd) == 0 || (fd) == 1 || (fd) == 2)

struct getdents64_ctx {
    struct dir_context ctx;
    char *buf;
    size_t count;
    size_t written;
};

static int filldir_getdents64(struct dir_context *dctx, const char *name, int namlen,
                              loff_t pos, ino_t ino, unsigned type) {
    struct getdents64_ctx *gctx = (struct getdents64_ctx *)dctx;
    size_t reclen = ALIGN_UP(sizeof(struct linux_dirent64) + (size_t)namlen + 1, 8);
    if (gctx->written + reclen > gctx->count) {
        return 1;
    }

    struct linux_dirent64 *d = (struct linux_dirent64 *)(gctx->buf + gctx->written);
    d->d_ino = ino;
    d->d_off = pos;
    d->d_reclen = (unsigned short)reclen;
    d->d_type = (unsigned char)type;
    memcpy(d->d_name, name, namlen);
    d->d_name[namlen] = '\0';

    gctx->written += reclen;
    dctx->pos = pos + 1;
    return 0;
}

#define LINUX_REBOOT_MAGIC1   0xfee1deadU
#define LINUX_REBOOT_MAGIC2   0x28121969U
#define LINUX_REBOOT_CMD_RESTART   0x01234567U
#define LINUX_REBOOT_CMD_HALT      0xCDEF0123U
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321FEDCU
#define LINUX_REBOOT_CMD_CAD_ON    0x89ABCDEFU

/* Syscall table */
typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static syscall_fn_t syscall_table[NR_SYSCALLS];
static struct cpu_regs *current_syscall_regs = NULL;
static void tty_flush_input(void) {
    while (uart_getc_nonblock() >= 0) {
        /* Drain pending serial input bytes after Ctrl+C. */
    }
}

/* Syscall statistics (for debugging/profiling) */
static uint64_t syscall_counts[NR_SYSCALLS];
static int loader_trace_budget = 4096;

/* Initialize syscall table */
static void syscall_table_init(void) {
    /* Clear table */
    for (int i = 0; i < NR_SYSCALLS; i++) {
        syscall_table[i] = NULL;
        syscall_counts[i] = 0;
    }
    
    /* Register syscall handlers */
    syscall_table[SYS_READ] = (syscall_fn_t)sys_read;
    syscall_table[SYS_WRITE] = (syscall_fn_t)sys_write;
    syscall_table[SYS_WRITEV] = (syscall_fn_t)sys_writev;
    syscall_table[SYS_OPEN] = (syscall_fn_t)sys_open;
    syscall_table[SYS_CLOSE] = (syscall_fn_t)sys_close;
    syscall_table[SYS_STAT] = (syscall_fn_t)sys_stat;
    syscall_table[SYS_FSTAT] = (syscall_fn_t)sys_fstat;
    syscall_table[SYS_ACCESS] = (syscall_fn_t)sys_access;
    syscall_table[SYS_LSEEK] = (syscall_fn_t)sys_lseek;
    syscall_table[SYS_MPROTECT] = (syscall_fn_t)sys_mprotect;
    syscall_table[SYS_MMAP] = (syscall_fn_t)sys_mmap;
    syscall_table[SYS_MUNMAP] = (syscall_fn_t)sys_munmap;
    syscall_table[SYS_BRK] = (syscall_fn_t)sys_brk;
    syscall_table[SYS_SCHED_YIELD] = (syscall_fn_t)sys_sched_yield;
    syscall_table[SYS_FORK] = (syscall_fn_t)sys_fork;
    syscall_table[SYS_EXECVE] = (syscall_fn_t)sys_execve;
    syscall_table[SYS_EXIT] = (syscall_fn_t)sys_exit;
    syscall_table[SYS_EXIT_GROUP] = (syscall_fn_t)sys_exit_group;
    syscall_table[SYS_WAIT4] = (syscall_fn_t)sys_wait4;
    syscall_table[SYS_GETPID] = (syscall_fn_t)sys_getpid;
    syscall_table[SYS_GETTID] = (syscall_fn_t)sys_gettid;
    syscall_table[SYS_GETPPID] = (syscall_fn_t)sys_getppid;
    syscall_table[SYS_GETUID] = (syscall_fn_t)sys_getuid;
    syscall_table[SYS_GETGID] = (syscall_fn_t)sys_getgid;
    syscall_table[SYS_GETEUID] = (syscall_fn_t)sys_geteuid;
    syscall_table[SYS_GETEGID] = (syscall_fn_t)sys_getegid;
    syscall_table[SYS_SETUID] = (syscall_fn_t)sys_setuid;
    syscall_table[SYS_SETGID] = (syscall_fn_t)sys_setgid;
    syscall_table[SYS_SETREUID] = (syscall_fn_t)sys_setreuid;
    syscall_table[SYS_SETREGID] = (syscall_fn_t)sys_setregid;
    syscall_table[SYS_SETRESUID] = (syscall_fn_t)sys_setresuid;
    syscall_table[SYS_SETRESGID] = (syscall_fn_t)sys_setresgid;
    syscall_table[SYS_SETFSUID] = (syscall_fn_t)sys_setfsuid;
    syscall_table[SYS_SETFSGID] = (syscall_fn_t)sys_setfsgid;
    syscall_table[SYS_CHDIR] = (syscall_fn_t)sys_chdir;
    syscall_table[SYS_GETCWD] = (syscall_fn_t)sys_getcwd;
    syscall_table[SYS_MKDIR] = (syscall_fn_t)sys_mkdir;
    syscall_table[SYS_RMDIR] = (syscall_fn_t)sys_rmdir;
    syscall_table[SYS_UNLINK] = (syscall_fn_t)sys_unlink;
    syscall_table[SYS_LINK] = (syscall_fn_t)sys_link;
    syscall_table[SYS_RENAME] = (syscall_fn_t)sys_rename;
    syscall_table[SYS_OPENAT] = (syscall_fn_t)sys_openat;
    syscall_table[SYS_NEWFSTATAT] = (syscall_fn_t)sys_newfstatat;
    syscall_table[SYS_READLINKAT] = (syscall_fn_t)sys_readlinkat;
    syscall_table[SYS_FACCESSAT] = (syscall_fn_t)sys_faccessat;
    syscall_table[SYS_DUP] = (syscall_fn_t)sys_dup;
    syscall_table[SYS_DUP2] = (syscall_fn_t)sys_dup2;
    syscall_table[SYS_PIPE] = (syscall_fn_t)sys_pipe;
    syscall_table[SYS_IOCTL] = (syscall_fn_t)sys_ioctl;
    syscall_table[SYS_FCNTL] = (syscall_fn_t)sys_fcntl;
    syscall_table[SYS_GETDENTS64] = (syscall_fn_t)sys_getdents64;
    syscall_table[SYS_KILL] = (syscall_fn_t)sys_kill;
    syscall_table[SYS_UNAME] = (syscall_fn_t)sys_uname;
    syscall_table[SYS_REBOOT] = (syscall_fn_t)sys_reboot;
    syscall_table[SYS_OBELISK_SYSCTL] = (syscall_fn_t)sys_sysctl;
}

static int process_getcwd_path(struct process *proc, char *buf, size_t size) {
    char tmp[PATH_MAX];
    size_t pos = PATH_MAX - 1;
    struct dentry *d;

    if (!proc || !buf || size == 0) {
        return -EINVAL;
    }

    tmp[pos] = '\0';

    if (!proc->cwd || !proc->root || proc->cwd == proc->root) {
        if (size < 2) {
            return -ERANGE;
        }
        buf[0] = '/';
        buf[1] = '\0';
        return 1;
    }

    d = proc->cwd;
    while (d && d != proc->root) {
        size_t n = strlen(d->d_name);
        if (n == 0) {
            break;
        }
        if (pos <= n) {
            return -ENAMETOOLONG;
        }
        pos -= n;
        memcpy(&tmp[pos], d->d_name, n);
        tmp[--pos] = '/';
        d = d->d_parent;
    }

    if (pos == PATH_MAX - 1) {
        tmp[--pos] = '/';
    }

    size_t out_len = (PATH_MAX - 1) - pos;
    if (out_len + 1 > size) {
        return -ERANGE;
    }

    memcpy(buf, &tmp[pos], out_len + 1);
    return (int)out_len;
}

static int resolve_user_path(const char *path, char *out, size_t out_size) {
    struct process *proc = current;
    char cwd[PATH_MAX];

    if (!path || !out || out_size == 0) {
        return -EFAULT;
    }

    if (path[0] == '/') {
        size_t n = strlen(path);
        if (n + 1 > out_size) {
            return -ENAMETOOLONG;
        }
        memcpy(out, path, n + 1);
        return 0;
    }

    int ret = process_getcwd_path(proc, cwd, sizeof(cwd));
    if (ret < 0) {
        return ret;
    }

    if (strcmp(cwd, "/") == 0) {
        if (snprintf(out, out_size, "/%s", path) >= (int)out_size) {
            return -ENAMETOOLONG;
        }
    } else {
        if (snprintf(out, out_size, "%s/%s", cwd, path) >= (int)out_size) {
            return -ENAMETOOLONG;
        }
    }

    return 0;
}

static int resolve_user_at_path(int dirfd, const char *path, char *out, size_t out_size) {
    /* Minimal compat: AT_FDCWD and absolute paths are supported. */
    if (dirfd == -100 || (path && path[0] == '/')) {
        return resolve_user_path(path, out, out_size);
    }
    return -ENOSYS;
}

static bool path_ends_with(const char *s, const char *suffix) {
    size_t slen, tlen;
    if (!s || !suffix) return false;
    slen = strlen(s);
    tlen = strlen(suffix);
    if (slen < tlen) return false;
    return strcmp(s + (slen - tlen), suffix) == 0;
}

static void kstat_to_uapi_stat(const struct kstat *in, struct stat *out) {
    memset(out, 0, sizeof(*out));
    out->st_dev = in->dev;
    out->st_ino = in->ino;
    out->st_mode = in->mode;
    out->st_nlink = in->nlink;
    out->st_uid = in->uid;
    out->st_gid = in->gid;
    out->st_rdev = in->rdev;
    out->st_size = in->size;
    out->st_blksize = in->blksize;
    out->st_blocks = in->blocks;
    out->st_atime = in->atime;
    out->st_mtime = in->mtime;
    out->st_ctime = in->ctime;
}

/* Main syscall dispatcher (called from assembly) */
int64_t syscall_dispatch(uint64_t syscall_num, struct cpu_regs *regs) {
    /* Check syscall number bounds */
    if (syscall_num >= NR_SYSCALLS) {
        printk(KERN_WARNING "Invalid syscall: %lu\n", syscall_num);
        return -ENOSYS;
    }
    
    /* Get handler */
    syscall_fn_t handler = syscall_table[syscall_num];
    if (!handler) {
        printk(KERN_WARNING "Unimplemented syscall: %lu\n", syscall_num);
        return -ENOSYS;
    }
    
    /* Update statistics */
    syscall_counts[syscall_num]++;
    
    /* Extract arguments from registers */
    uint64_t arg1 = regs->rdi;
    uint64_t arg2 = regs->rsi;
    uint64_t arg3 = regs->rdx;
    uint64_t arg4 = regs->r10;  /* Note: r10 instead of rcx */
    uint64_t arg5 = regs->r8;
    uint64_t arg6 = regs->r9;
    
    int trace_loader = 0;
    if (loader_trace_budget > 0 && current && current->pid == 2) {
        const char *comm = current->comm;
        if ((comm && (strcmp(comm, "zsh") == 0 || strncmp(comm, "ld-linux", 8) == 0)) ||
            syscall_num == SYS_EXECVE) {
            trace_loader = 1;
            loader_trace_budget--;
            printk(KERN_INFO,
                   "syscall-trace: comm=%s nr=%lu a1=0x%lx a2=0x%lx a3=0x%lx a4=0x%lx\n",
                   comm ? comm : "?", syscall_num, arg1, arg2, arg3, arg4);
        }
    }

    current_syscall_regs = regs;
    /* Call handler */
    int64_t ret = handler(arg1, arg2, arg3, arg4, arg5, arg6);
    current_syscall_regs = NULL;
    if (trace_loader) {
        printk(KERN_INFO, "syscall-trace: nr=%lu ret=%ld\n", syscall_num, ret);
    }
    return ret;
}

/* ==========================================================================
 * Syscall implementations (stubs for now)
 * ========================================================================== */

static int64_t sys_read(int fd, void *buf, size_t count) {
    struct process *proc = current;
    struct file *file = NULL;

    if (!buf) {
        return -EFAULT;
    }

    if (proc && proc->files && fd >= 0 && fd < (int)proc->files->max_fds) {
        file = fd_get(proc->files, fd);
    }
    if (file) {
        return vfs_read(file, buf, count, NULL);
    }
    if (fd != 0) {
        return -EBADF;
    }
    if (count == 0) {
        return 0;
    }

    char *out = (char *)buf;

    /* Character mode for interactive shells/editors. */
    if (count == 1) {
        char c = uart_getc();
        if (c == '\r') {
            c = '\n';
        }
        if (c == 0x03) {
            console_write("^C\n", 3);
            tty_flush_input();
            return -EINTR;
        }
        out[0] = c;
        return 1;
    }

    size_t i = 0;
    while (i < count) {
        char c = uart_getc();
        if (c == '\r') {
            c = '\n';
        }

        /* Ctrl+C interrupts the current foreground read. */
        if (c == 0x03) {
            console_write("^C\n", 3);
            tty_flush_input();
            return -EINTR;
        }

        /* Basic line editing for serial TTY use. */
        if ((c == '\b' || c == 0x7F) && i > 0) {
            i--;
            console_write("\b \b", 3);
            continue;
        }

        out[i++] = c;
        console_putc(c);

        if (c == '\n') {
            break;
        }
    }

    return (int64_t)i;
}

static int64_t sys_write(int fd, const void *buf, size_t count) {
    struct process *proc = current;
    struct file *file = NULL;

    if (!buf) {
        return -EFAULT;
    }

    if (proc && proc->files && fd >= 0 && fd < (int)proc->files->max_fds) {
        file = fd_get(proc->files, fd);
    }
    if (file) {
        return vfs_write(file, buf, count, NULL);
    }

    /* Mirror userspace output to serial and VGA for default stdio. */
    if (fd == 1 || fd == 2) {
        const char *p = (const char *)buf;
        console_write(p, count);
        return count;
    }
    return -EBADF;
}

static int64_t sys_writev(int fd, const void *iov, int iovcnt) {
    const struct iovec *vec = (const struct iovec *)iov;
    int64_t total = 0;

    if (!vec || iovcnt < 0) {
        return -EINVAL;
    }
    if (iovcnt > 1024) {
        return -EINVAL;
    }

    for (int i = 0; i < iovcnt; i++) {
        if (!vec[i].iov_base && vec[i].iov_len != 0) {
            return -EFAULT;
        }
        if (vec[i].iov_len == 0) {
            continue;
        }
        int64_t n = sys_write(fd, vec[i].iov_base, vec[i].iov_len);
        if (n < 0) {
            return (total > 0) ? total : n;
        }
        total += n;
        if ((size_t)n < vec[i].iov_len) {
            break;
        }
    }
    return total;
}

static int64_t sys_open(const char *pathname, int flags, mode_t mode) {
    struct process *proc = current;
    struct file *file;
    char resolved[PATH_MAX];
    int ret;

    if (!proc || !proc->files) {
        return -EBADF;
    }
    ret = resolve_user_path(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }

    file = vfs_open(resolved, flags, mode);
    if (IS_ERR(file)) {
        return PTR_ERR(file);
    }

    ret = fd_alloc(proc->files, file, (flags & O_CLOEXEC) ? O_CLOEXEC : 0);
    if (ret < 0) {
        vfs_close(file);
        return ret;
    }

    return ret;
}

static int64_t sys_close(int fd) {
    struct process *proc = current;
    struct file *file;
    if (!proc || !proc->files) {
        return -EBADF;
    }
    if (fd < 0 || fd >= (int)proc->files->max_fds) {
        return -EBADF;
    }
    file = fd_get(proc->files, fd);
    if (!file) {
        return -EBADF;
    }
    proc->files->fds[fd].file = NULL;
    proc->files->fds[fd].flags = 0;
    if (fd >= 3 && fd < (int)proc->files->next_fd) {
        proc->files->next_fd = fd;
        if (proc->files->next_fd < 3) {
            proc->files->next_fd = 3;
        }
    }
    put_file(file);
    return 0;
}

static int64_t sys_stat(const char *pathname, struct stat *statbuf) {
    struct kstat kst;
    char resolved[PATH_MAX];
    int ret;
    if (!statbuf) {
        return -EFAULT;
    }
    ret = resolve_user_path(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    ret = vfs_stat(resolved, &kst);
    if (ret < 0) {
        return ret;
    }
    kstat_to_uapi_stat(&kst, statbuf);
    return 0;
}

static int64_t sys_fstat(int fd, struct stat *statbuf) {
    struct process *proc = current;
    struct file *file;
    struct kstat kst;
    int ret;
    if (!statbuf) {
        return -EFAULT;
    }
    if (!proc || !proc->files) {
        return -EBADF;
    }
    file = fd_get(proc->files, fd);
    if (!file) {
        return -EBADF;
    }
    ret = vfs_fstat(file, &kst);
    if (ret < 0) {
        return ret;
    }
    kstat_to_uapi_stat(&kst, statbuf);
    return 0;
}

static int64_t sys_access(const char *pathname, int mode) {
    char resolved[PATH_MAX];
    struct dentry *dentry;
    int ret = resolve_user_path(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    (void)mode;
    dentry = vfs_lookup(resolved);
    if (!dentry) {
        return -ENOENT;
    }
    dput(dentry);
    return 0;
}

static int64_t sys_lseek(int fd, off_t offset, int whence) {
    struct process *proc = current;
    struct file *file;
    if (!proc || !proc->files) {
        return -EBADF;
    }
    file = fd_get(proc->files, fd);
    if (!file) {
        return -EBADF;
    }
    return vfs_lseek(file, offset, whence);
}

static int64_t sys_mprotect(void *addr, size_t len, int prot) {
    struct process *proc = current;
    if (!proc || !proc->mm) {
        return -EINVAL;
    }
    return vmm_mprotect(proc->mm, addr, len, prot);
}

static int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    struct process *proc = current;
    struct file *file = NULL;
    void *mapped;

    if (!proc || !proc->mm || length == 0) {
        return -EINVAL;
    }

    if (!(flags & MAP_ANONYMOUS)) {
        if (!proc->files) {
            return -EBADF;
        }
        file = fd_get(proc->files, fd);
        if (!file) {
            return -EBADF;
        }
    }

    mapped = vmm_mmap(proc->mm, addr, length, prot, flags, file, offset);
    if (mapped == MAP_FAILED) {
        return -ENOMEM;
    }

    uint64_t start = ALIGN_DOWN((uint64_t)mapped, PAGE_SIZE);
    uint64_t end = ALIGN_UP((uint64_t)mapped + length, PAGE_SIZE);
    uint64_t pte_flags = vmm_prot_to_pte_flags(prot);

    for (uint64_t vaddr = start; vaddr < end; vaddr += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            vmm_munmap(proc->mm, (void *)start, end - start);
            return -ENOMEM;
        }
        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        if (mmu_map(proc->mm->pt, vaddr, phys, pte_flags) != 0) {
            pmm_free_page(phys);
            vmm_munmap(proc->mm, (void *)start, end - start);
            return -ENOMEM;
        }
    }

    if (file) {
        uint64_t copied = 0;
        while (copied < length) {
            uint64_t dst_vaddr = (uint64_t)mapped + copied;
            uint64_t phys = mmu_resolve(proc->mm->pt, dst_vaddr);
            if (!phys) {
                return -EFAULT;
            }
            size_t page_off = (size_t)(dst_vaddr & (PAGE_SIZE - 1));
            size_t chunk = MIN((uint64_t)(PAGE_SIZE - page_off), (uint64_t)(length - copied));
            off_t read_off = offset + copied;
            int ret = vfs_read(file, (uint8_t *)PHYS_TO_VIRT(phys) + page_off, chunk, &read_off);
            if (ret <= 0) {
                break;
            }
            copied += (size_t)ret;
            if ((size_t)ret < chunk) {
                break;
            }
        }
    }

    return (int64_t)(uint64_t)mapped;
}

static int64_t sys_munmap(void *addr, size_t length) {
    struct process *proc = current;
    if (!proc || !proc->mm) {
        return -EINVAL;
    }
    return vmm_munmap(proc->mm, addr, length);
}

static int64_t sys_brk(void *addr) {
    struct process *proc = current;
    void *ret;
    if (!proc || !proc->mm) {
        return 0;
    }
    ret = vmm_brk(proc->mm, addr);
    if (ret == (void *)-1) {
        return (int64_t)(uint64_t)proc->mm->brk_end;
    }
    return (int64_t)(uint64_t)ret;
}

static int64_t sys_fork(void) {
    if (!current_syscall_regs) {
        return -EINVAL;
    }
    return do_fork(0, 0, current_syscall_regs);
}

static int64_t sys_sched_yield(void) {
    scheduler_yield();
    return 0;
}

static int64_t sys_execve(const char *pathname, char *const argv[], char *const envp[]) {
    if (!pathname) {
        return -EFAULT;
    }
    return do_execve(pathname, argv, envp);
}

static int64_t sys_exit(int status) {
    struct process *p = current;
    if (p && p->pid == 2) {
        static char *const rescue_argv[] = { "busybox", "sh", NULL };
        static char *const rescue_envp[] = {
            "PATH=/bin:/sbin:/usr/bin",
            "HOME=/",
            "SHELL=/bin/sh",
            "TERM=vt100",
            NULL
        };
        printk(KERN_ERR "init: pid1 attempted exit(%d), starting rescue shell\n", status);
        if (do_execve("/bin/busybox", rescue_argv, rescue_envp) >= 0) {
            __builtin_unreachable();
        }
        printk(KERN_ERR "init: rescue shell exec failed, continuing exit path\n");
    }

    if (p) {
        p->exit_code = status;
    }
    scheduler_exit();
    __builtin_unreachable();
}

static int64_t sys_exit_group(int status) {
    return sys_exit(status);
}

static int64_t sys_wait4(pid_t pid, int *wstatus, int options, void *rusage) {
    (void)rusage;
    return do_wait(pid, wstatus, options);
}

static int64_t sys_getpid(void) {
    struct process *p = current;
    return p ? p->pid : 0;
}

static int64_t sys_gettid(void) {
    struct process *p = current;
    return p ? p->pid : 0;
}

static int64_t sys_getppid(void) {
    return 0;
}

static int64_t sys_getuid(void) {
    struct process *p = current;
    if (!p || !p->cred) return 0;
    return p->cred->uid;
}

static int64_t sys_getgid(void) {
    struct process *p = current;
    if (!p || !p->cred) return 0;
    return p->cred->gid;
}

static int64_t sys_geteuid(void) {
    struct process *p = current;
    if (!p || !p->cred) return 0;
    return p->cred->euid;
}

static int64_t sys_getegid(void) {
    struct process *p = current;
    if (!p || !p->cred) return 0;
    return p->cred->egid;
}

static bool cred_uid_match(struct cred *c, uid_t uid) {
    return (c->uid == uid || c->euid == uid || c->suid == uid);
}

static bool cred_gid_match(struct cred *c, gid_t gid) {
    return (c->gid == gid || c->egid == gid || c->sgid == gid);
}

static int64_t sys_setuid(uid_t uid) {
    struct process *p = current;
    if (!p || !p->cred) return -ESRCH;
    struct cred *c = p->cred;
    if (c->euid == 0) {
        c->uid = uid;
        c->euid = uid;
        c->suid = uid;
        c->fsuid = uid;
        return 0;
    }
    if (!cred_uid_match(c, uid)) return -EPERM;
    c->euid = uid;
    c->fsuid = uid;
    return 0;
}

static int64_t sys_setgid(gid_t gid) {
    struct process *p = current;
    if (!p || !p->cred) return -ESRCH;
    struct cred *c = p->cred;
    if (c->euid == 0) {
        c->gid = gid;
        c->egid = gid;
        c->sgid = gid;
        c->fsgid = gid;
        return 0;
    }
    if (!cred_gid_match(c, gid)) return -EPERM;
    c->egid = gid;
    c->fsgid = gid;
    return 0;
}

static int64_t sys_setreuid(uid_t ruid, uid_t euid) {
    struct process *p = current;
    if (!p || !p->cred) return -ESRCH;
    struct cred *c = p->cred;
    const uid_t U_NONE = (uid_t)-1;
    if (c->euid != 0) {
        if ((ruid != U_NONE && !cred_uid_match(c, ruid)) ||
            (euid != U_NONE && !cred_uid_match(c, euid))) {
            return -EPERM;
        }
    }
    if (ruid != U_NONE) c->uid = ruid;
    if (euid != U_NONE) {
        c->euid = euid;
        c->fsuid = euid;
    }
    c->suid = c->euid;
    return 0;
}

static int64_t sys_setregid(gid_t rgid, gid_t egid) {
    struct process *p = current;
    if (!p || !p->cred) return -ESRCH;
    struct cred *c = p->cred;
    const gid_t G_NONE = (gid_t)-1;
    if (c->euid != 0) {
        if ((rgid != G_NONE && !cred_gid_match(c, rgid)) ||
            (egid != G_NONE && !cred_gid_match(c, egid))) {
            return -EPERM;
        }
    }
    if (rgid != G_NONE) c->gid = rgid;
    if (egid != G_NONE) {
        c->egid = egid;
        c->fsgid = egid;
    }
    c->sgid = c->egid;
    return 0;
}

static int64_t sys_setresuid(uid_t ruid, uid_t euid, uid_t suid) {
    struct process *p = current;
    if (!p || !p->cred) return -ESRCH;
    struct cred *c = p->cred;
    const uid_t U_NONE = (uid_t)-1;
    if (c->euid != 0) {
        if ((ruid != U_NONE && !cred_uid_match(c, ruid)) ||
            (euid != U_NONE && !cred_uid_match(c, euid)) ||
            (suid != U_NONE && !cred_uid_match(c, suid))) {
            return -EPERM;
        }
    }
    if (ruid != U_NONE) c->uid = ruid;
    if (euid != U_NONE) {
        c->euid = euid;
        c->fsuid = euid;
    }
    if (suid != U_NONE) c->suid = suid;
    return 0;
}

static int64_t sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    struct process *p = current;
    if (!p || !p->cred) return -ESRCH;
    struct cred *c = p->cred;
    const gid_t G_NONE = (gid_t)-1;
    if (c->euid != 0) {
        if ((rgid != G_NONE && !cred_gid_match(c, rgid)) ||
            (egid != G_NONE && !cred_gid_match(c, egid)) ||
            (sgid != G_NONE && !cred_gid_match(c, sgid))) {
            return -EPERM;
        }
    }
    if (rgid != G_NONE) c->gid = rgid;
    if (egid != G_NONE) {
        c->egid = egid;
        c->fsgid = egid;
    }
    if (sgid != G_NONE) c->sgid = sgid;
    return 0;
}

static int64_t sys_setfsuid(uid_t fsuid) {
    struct process *p = current;
    if (!p || !p->cred) return -ESRCH;
    struct cred *c = p->cred;
    uid_t old = c->fsuid;
    if (c->euid == 0 || cred_uid_match(c, fsuid)) {
        c->fsuid = fsuid;
    }
    return old;
}

static int64_t sys_setfsgid(gid_t fsgid) {
    struct process *p = current;
    if (!p || !p->cred) return -ESRCH;
    struct cred *c = p->cred;
    gid_t old = c->fsgid;
    if (c->euid == 0 || cred_gid_match(c, fsgid)) {
        c->fsgid = fsgid;
    }
    return old;
}

static int64_t sys_chdir(const char *path) {
    struct process *proc = current;
    char resolved[PATH_MAX];
    struct dentry *dentry;
    int ret;

    if (!proc) {
        return -ESRCH;
    }
    ret = resolve_user_path(path, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    dentry = vfs_lookup(resolved);
    if (!dentry) {
        return -ENOENT;
    }
    if (!dentry->d_inode || !S_ISDIR(dentry->d_inode->i_mode)) {
        dput(dentry);
        return -ENOTDIR;
    }

    if (proc->cwd) {
        dput(proc->cwd);
    }
    proc->cwd = dentry;
    return 0;
}

static int64_t sys_getcwd(char *buf, size_t size) {
    char cwd[PATH_MAX];
    int ret;

    if (!buf || size == 0) {
        return -EINVAL;
    }
    ret = process_getcwd_path(current, cwd, sizeof(cwd));
    if (ret < 0) {
        return ret;
    }
    if ((size_t)(ret + 1) > size) {
        return -ERANGE;
    }
    memcpy(buf, cwd, ret + 1);
    return ret + 1;
}

static int64_t sys_mkdir(const char *pathname, mode_t mode) {
    char resolved[PATH_MAX];
    int ret = resolve_user_path(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    return vfs_mkdir(resolved, mode);
}

static int64_t sys_rmdir(const char *pathname) {
    char resolved[PATH_MAX];
    int ret = resolve_user_path(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    return vfs_rmdir(resolved);
}

static int64_t sys_unlink(const char *pathname) {
    char resolved[PATH_MAX];
    int ret = resolve_user_path(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    return vfs_unlink(resolved);
}

static int64_t sys_link(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath;
    return -ENOSYS;
}

static int64_t sys_rename(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath;
    return -ENOSYS;
}

static int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    struct process *proc = current;
    struct file *file;
    char resolved[PATH_MAX];
    int ret;

    if (!proc || !proc->files) {
        return -EBADF;
    }
    ret = resolve_user_at_path(dirfd, pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }

    file = vfs_open(resolved, flags, mode);
    if (IS_ERR(file)) {
        return PTR_ERR(file);
    }
    ret = fd_alloc(proc->files, file, (flags & O_CLOEXEC) ? O_CLOEXEC : 0);
    if (ret < 0) {
        vfs_close(file);
        return ret;
    }
    return ret;
}

static int64_t sys_newfstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    struct kstat kst;
    char resolved[PATH_MAX];
    int ret;
    (void)flags;
    if (!statbuf) {
        return -EFAULT;
    }
    ret = resolve_user_at_path(dirfd, pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    ret = vfs_stat(resolved, &kst);
    if (ret < 0) {
        return ret;
    }
    kstat_to_uapi_stat(&kst, statbuf);
    return 0;
}

static int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    const char *target = NULL;
    size_t n;
    (void)dirfd;

    if (!buf || bufsiz == 0) {
        return -EINVAL;
    }

    if (!pathname) {
        pathname = "/proc/self/exe";
    }

    if (strcmp(pathname, "/proc/self/exe") == 0 ||
        strcmp(pathname, "self/exe") == 0 ||
        strcmp(pathname, "exe") == 0 ||
        strcmp(pathname, "/proc/thread-self/exe") == 0 ||
        path_ends_with(pathname, "/self/exe")) {
        struct process *proc = current;
        if (proc && proc->exec_path[0]) {
            target = proc->exec_path;
        } else {
            target = "/sbin/init";
        }
    } else {
        return -ENOENT;
    }

    n = strlen(target);
    if (n > bufsiz) {
        n = bufsiz;
    }
    memcpy(buf, target, n);
    return (int64_t)n;
}

static int64_t sys_faccessat(int dirfd, const char *pathname, int mode) {
    char resolved[PATH_MAX];
    struct dentry *dentry;
    int ret = resolve_user_at_path(dirfd, pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    (void)mode;
    dentry = vfs_lookup(resolved);
    if (!dentry) {
        return -ENOENT;
    }
    dput(dentry);
    return 0;
}

static int64_t sys_dup(int oldfd) {
    struct process *proc = current;
    struct file *file;
    int newfd;
    if (!proc || !proc->files) {
        return -EBADF;
    }
    if (oldfd < 0 || oldfd >= (int)proc->files->max_fds) {
        return -EBADF;
    }
    file = fd_get(proc->files, oldfd);
    if (!file) {
        return -EBADF;
    }
    newfd = fd_alloc(proc->files, file, proc->files->fds[oldfd].flags);
    if (newfd < 0) {
        return newfd;
    }
    get_file(file);
    return newfd;
}

static int64_t sys_dup2(int oldfd, int newfd) {
    struct process *proc = current;
    struct file *file, *old_target;
    if (!proc || !proc->files) {
        return -EBADF;
    }
    if (oldfd < 0 || oldfd >= (int)proc->files->max_fds) {
        return -EBADF;
    }
    if (newfd < 0 || newfd >= (int)proc->files->max_fds) {
        return -EBADF;
    }
    file = fd_get(proc->files, oldfd);
    if (!file) {
        return -EBADF;
    }
    if (oldfd == newfd) {
        return newfd;
    }

    old_target = fd_get(proc->files, newfd);
    if (old_target) {
        proc->files->fds[newfd].file = NULL;
        proc->files->fds[newfd].flags = 0;
        put_file(old_target);
    }

    get_file(file);
    proc->files->fds[newfd].file = file;
    proc->files->fds[newfd].flags = proc->files->fds[oldfd].flags;
    return newfd;
}

static int64_t sys_pipe(int pipefd[2]) {
    (void)pipefd;
    return -ENOSYS;
}

static int64_t sys_ioctl(int fd, unsigned long request, void *arg) {
    if (!arg) {
        return -EINVAL;
    }

    switch (request) {
        case TCGETS: {
            struct termios_compat t;
            memset(&t, 0, sizeof(t));
            /* Canonical mode, echo, signal handling. */
            t.c_lflag = 0x00000001U | 0x00000002U | 0x00000008U; /* ISIG|ICANON|ECHO */
            t.c_cc[6] = 1;   /* VMIN */
            t.c_cc[5] = 0;   /* VTIME */
            memcpy(arg, &t, sizeof(t));
            return 0;
        }
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            return 0;
        case TIOCGWINSZ: {
            struct winsize ws;
            ws.ws_row = 25;
            ws.ws_col = 80;
            ws.ws_xpixel = 0;
            ws.ws_ypixel = 0;
            memcpy(arg, &ws, sizeof(ws));
            return 0;
        }
        default:
            /* tty-centric compatibility path: unknown requests on stdio are ignored. */
            if (IS_STDIO_FD(fd)) {
                return -EINVAL;
            }
            return -ENOTTY;
    }
}

static int64_t sys_fcntl(int fd, int cmd, uint64_t arg) {
    (void)arg;
    if (fd < 0) {
        return -EBADF;
    }
    switch (cmd) {
        case F_GETFD:
            return 0;
        case F_SETFD:
            return 0;
        case F_GETFL:
            if (IS_STDIO_FD(fd)) {
                return O_RDWR;
            }
            return O_RDWR;
        case F_SETFL:
            return 0;
        default:
            return -EINVAL;
    }
}

static int64_t sys_getdents64(int fd, void *dirp, size_t count) {
    struct process *proc = current;
    struct file *file;
    struct getdents64_ctx ctx;
    int ret;

    if (!dirp || count == 0) {
        return -EINVAL;
    }
    if (!proc || !proc->files) {
        return -EBADF;
    }
    file = fd_get(proc->files, fd);
    if (!file) {
        return -EBADF;
    }
    if (!file->f_op || !file->f_op->readdir) {
        return -ENOTDIR;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.buf = (char *)dirp;
    ctx.count = count;
    ctx.written = 0;
    ctx.ctx.pos = file->f_pos;
    ctx.ctx.actor = filldir_getdents64;

    ret = file->f_op->readdir(file, &ctx.ctx);
    if (ret < 0) {
        return ret;
    }
    file->f_pos = ctx.ctx.pos;
    return (int64_t)ctx.written;
}

static int64_t sys_kill(pid_t pid, int sig) {
    (void)pid; (void)sig;
    return -ENOSYS;
}

static int64_t sys_uname(void *buf) {
    struct utsname {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    } *uname = buf;
    
    strcpy(uname->sysname, "Obelisk");
    strcpy(uname->nodename, "obelisk");
    strcpy(uname->release, OBELISK_VERSION_STRING);
    strcpy(uname->version, "From Axioms, Order");
    strcpy(uname->machine, "x86_64");
    strcpy(uname->domainname, "(none)");
    
    return 0;
}

static __noreturn void machine_reboot_now(void) {
    printk(KERN_INFO "reboot: requesting hardware reset via 8042\n");
    cli();

    /* Wait until keyboard controller input buffer is clear. */
    for (int i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
        pause();
    }

    /* Pulse CPU reset line. */
    outb(0x64, 0xFE);

    /* If reset does not happen immediately, halt safely. */
    for (;;) {
        hlt();
    }
}

static __noreturn void machine_poweroff_now(void) {
    printk(KERN_INFO "shutdown: requesting ACPI/QEMU poweroff\n");
    cli();
    /* QEMU/Bochs poweroff ports (best effort). */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    for (;;) {
        hlt();
    }
}

static int64_t sys_reboot(int magic1, int magic2, unsigned int cmd, void *arg) {
    (void)arg;
    /* Linux-compatible magic values so userland can call reboot sanely. */
    if ((uint32_t)magic1 != LINUX_REBOOT_MAGIC1 || (uint32_t)magic2 != LINUX_REBOOT_MAGIC2) {
        return -EINVAL;
    }

    if (cmd == LINUX_REBOOT_CMD_RESTART || cmd == LINUX_REBOOT_CMD_CAD_ON) {
        machine_reboot_now();
    }
    if (cmd == LINUX_REBOOT_CMD_POWER_OFF || cmd == LINUX_REBOOT_CMD_HALT) {
        machine_poweroff_now();
    }

    return -EINVAL;
}

static int64_t sys_sysctl(void *args) {
    /* TODO: Implement sysctl interface */
    (void)args;
    return -ENOSYS;
}

/* ==========================================================================
 * Syscall initialization
 * ========================================================================== */

/* Set up SYSCALL/SYSRET MSRs */
static void syscall_msr_init(void) {
    /* Enable SYSCALL instruction */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= MSR_EFER_SCE;
    wrmsr(MSR_EFER, efer);
    
    /* Set up STAR MSR (segment selectors) */
    /* STAR[31:0] = reserved
     * STAR[47:32] = kernel CS (also sets kernel SS = CS + 8)
     * STAR[63:48] = user CS - 16 (also sets user SS) */
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x1B << 48);
    wrmsr(MSR_STAR, star);
    
    /* Set LSTAR to syscall entry point */
    extern void syscall_entry(void);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    
    /* Set SFMASK (RFLAGS bits to clear on syscall) */
    wrmsr(MSR_SFMASK, EFLAGS_IF | EFLAGS_TF | EFLAGS_DF);
}

/* Initialize syscall subsystem */
void syscall_init(void) {
    printk(KERN_INFO "Initializing syscall interface...\n");
    
    syscall_table_init();
    syscall_msr_init();
    
    printk(KERN_INFO "Syscall interface initialized (%d syscalls registered)\n",
           NR_SYSCALLS);
}