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
#include <obelisk/zig_path.h>
#include <obelisk/zig_safe_arith.h>
#include <obelisk/zig_exec_string.h>
#include <obelisk/power.h>
#include <sysctl/sysctl.h>
#include <net/net.h>

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
/* Linux x86_64 syscall #17: pread64(fd, buf, count, offset) */
static int64_t sys_pread64(int fd, void *buf, size_t count, loff_t offset);
static int64_t sys_mprotect(void *addr, size_t len, int prot);
static int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
static int64_t sys_munmap(void *addr, size_t length);
static int64_t sys_brk(void *addr);
static int64_t sys_sched_yield(void);
static int64_t sys_rt_sigaction(int signum, const void *act, void *oldact, size_t sigsetsize);
static int64_t sys_rt_sigprocmask(int how, const void *set, void *oldset, size_t sigsetsize);
static int64_t sys_clone(uint64_t flags, uint64_t child_stack, void *parent_tid, void *child_tid, uint64_t tls);
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
static int64_t sys_umask(mode_t mask);
static int64_t sys_setpgid(pid_t pid, pid_t pgid);
static int64_t sys_getpgrp(void);
static int64_t sys_setsid(void);
static int64_t sys_getpgid(pid_t pid);
static int64_t sys_getsid(pid_t pid);
static int64_t sys_chdir(const char *path);
static int64_t sys_getcwd(char *buf, size_t size);
static int64_t sys_mkdir(const char *pathname, mode_t mode);
static int64_t sys_rmdir(const char *pathname);
static int64_t sys_unlink(const char *pathname);
static int64_t sys_chmod(const char *pathname, mode_t mode);
static int64_t sys_chown(const char *pathname, uid_t owner, gid_t group);
static int64_t sys_link(const char *oldpath, const char *newpath);
static int64_t sys_rename(const char *oldpath, const char *newpath);
static int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode);
static int64_t sys_newfstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags);
static int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
static int64_t sys_faccessat(int dirfd, const char *pathname, int mode);
static int64_t sys_dup(int oldfd);
static int64_t sys_dup2(int oldfd, int newfd);
static int64_t sys_pipe(int pipefd[2]);
static int64_t sys_poll(void *fds, uint64_t nfds, int timeout);
static int64_t sys_select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);
static int64_t sys_nanosleep(const void *req, void *rem);
static int64_t sys_ioctl(int fd, unsigned long request, void *arg);
static int64_t sys_fcntl(int fd, int cmd, uint64_t arg);
static int64_t sys_getdents64(int fd, void *dirp, size_t count);
static int64_t sys_kill(pid_t pid, int sig);
static int64_t sys_uname(void *buf);
static int64_t sys_reboot(int magic1, int magic2, unsigned int cmd, void *arg);
static int64_t sys_statfs(const char *pathname, void *buf);
static int64_t sys_mount(char *source, char *target, char *fstype, unsigned long flags, void *data);
static int64_t sys_umount2(const char *target, int flags);
static int64_t sys_obelisk_proc_list(void *ubuf, size_t *ulenp);
static int64_t sys_sysctl(void *args);
static int64_t sys_socket(int domain, int type, int protocol);
static int64_t sys_connect(int sockfd, const void *addr, int addrlen);
static int64_t sys_accept(int sockfd, void *addr, int *addrlen);
static int64_t sys_sendto(int sockfd, const void *buf, size_t len, int flags, const void *dest_addr, int addrlen);
static int64_t sys_recvfrom(int sockfd, void *buf, size_t len, int flags, void *src_addr, int *addrlen);
static int64_t sys_bind(int sockfd, const void *addr, int addrlen);
static int64_t sys_listen(int sockfd, int backlog);
static int64_t sys_shutdown(int sockfd, int how);
static int64_t sys_socketpair(int domain, int type, int protocol, int *sv);
static int64_t sys_arch_prctl(int code, uint64_t addr);
/* Linux x86_64 syscall gaps needed by glibc/ld.so early startup */
static int64_t sys_set_tid_address(uint64_t tidptr);
static int64_t sys_set_robust_list(uint64_t head, uint64_t len);
static int64_t sys_prlimit64(uint64_t pid, uint64_t new_limit, uint64_t old_limit, uint64_t flags);
static int64_t sys_getrandom(void *buf, size_t buflen, uint64_t flags);
static int64_t sys_clock_gettime(uint64_t clockid, void *tp);
static int64_t sys_rseq(void *rseq, uint64_t rseq_len, uint64_t flags);
static int64_t sys_prctl(uint64_t code, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);
static int resolve_user_path(const char *path, char *out, size_t out_size);
static int resolve_user_at_path(int dirfd, const char *path, char *out, size_t out_size);

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

struct vt_stat_compat {
    uint16_t v_active;
    uint16_t v_signal;
    uint16_t v_state;
};

struct vt_mode_compat {
    uint8_t mode;
    uint8_t waitv;
    uint16_t relsig;
    uint16_t acqsig;
    uint16_t frsig;
};

struct sockaddr_in_compat {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
} __packed;

struct sockaddr_un_compat {
    uint16_t sun_family;
    char sun_path[108];
} __packed;

struct pollfd_compat {
    int fd;
    int16_t events;
    int16_t revents;
} __packed;

struct timespec_compat {
    int64_t tv_sec;
    int64_t tv_nsec;
} __packed;

struct rlimit_compat {
    /* Linux rlim_t is unsigned long on x86_64 (64-bit). */
    uint64_t rlim_cur;
    uint64_t rlim_max;
} __packed;

struct timeval_compat {
    int64_t tv_sec;
    int64_t tv_usec;
} __packed;

struct kernel_sigaction_compat {
    uint64_t sa_handler;
    uint64_t sa_flags;
    uint64_t sa_restorer;
    uint64_t sa_mask;
} __packed;

struct statfs_compat {
    int64_t f_type;
    int64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    struct {
        int32_t val[2];
    } f_fsid;
    int64_t f_namelen;
    int64_t f_frsize;
    int64_t f_flags;
    int64_t f_spare[4];
} __packed;

#define SIG_BLOCK_MASK      0
#define SIG_UNBLOCK_MASK    1
#define SIG_SETMASK_MASK    2

#define TCGETS      0x5401UL
#define TCSETS      0x5402UL
#define TCSETSW     0x5403UL
#define TCSETSF     0x5404UL
#define TIOCGWINSZ  0x5413UL
#define TIOCSCTTY   0x540EUL
#define TIOCNOTTY   0x5422UL
#define TIOCGPGRP   0x540FUL
#define TIOCSPGRP   0x5410UL

#define KDSETMODE   0x4B3AUL
#define KDGETMODE   0x4B3BUL
#define KDGKBMODE   0x4B44UL
#define KDSKBMODE   0x4B45UL
#define KD_TEXT     0
#define KD_GRAPHICS 1
#define K_XLATE     0

#define VT_OPENQRY    0x5600UL
#define VT_GETMODE    0x5601UL
#define VT_SETMODE    0x5602UL
#define VT_GETSTATE   0x5603UL
#define VT_ACTIVATE   0x5606UL
#define VT_WAITACTIVE 0x5607UL

#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4

#define IS_STDIO_FD(fd) ((fd) == 0 || (fd) == 1 || (fd) == 2)
#define AF_UNIX     1
#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define SOCKET_FD_BASE  512
#define SOCKET_MAX      64
#define UNIX_PATH_MAX   108
#define UNIX_RX_BUF     4096
#define UNIX_PENDING_MAX 8

#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010

struct ksocket {
    bool used;
    pid_t owner_pid;
    int domain;
    int type;
    int protocol;
    uint16_t bound_port;
    uint8_t remote_ip[4];
    uint16_t remote_port;
    int tcp_conn_id;
    bool connected;
    bool peer_closed;
    int peer_fd;
    bool listening;
    bool bound;
    char unix_path[UNIX_PATH_MAX];
    int pending_fds[UNIX_PENDING_MAX];
    uint8_t pending_head;
    uint8_t pending_tail;
    uint8_t rx_buf[UNIX_RX_BUF];
    size_t rx_head;
    size_t rx_len;
};

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

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

/* Syscall table */
typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static syscall_fn_t syscall_table[NR_SYSCALLS];
static struct cpu_regs *current_syscall_regs = NULL;
static struct ksocket socket_table[SOCKET_MAX];

static uint16_t bswap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}
static uint16_t htons16(uint16_t v) { return bswap16(v); }
static uint16_t ntohs16(uint16_t v) { return bswap16(v); }
static void tty_flush_input(void) {
    devfs_console_flush_input();
}

static int socket_fd_to_index(int fd) {
    int idx = fd - SOCKET_FD_BASE;
    if (idx < 0 || idx >= SOCKET_MAX) {
        return -1;
    }
    return idx;
}

static struct ksocket *socket_get_owned(int fd) {
    int idx = socket_fd_to_index(fd);
    if (idx < 0) {
        return NULL;
    }
    if (!socket_table[idx].used) {
        return NULL;
    }
    if (!current || socket_table[idx].owner_pid != current->pid) {
        return NULL;
    }
    return &socket_table[idx];
}

static struct ksocket *socket_get_any(int fd) {
    int idx = socket_fd_to_index(fd);
    if (idx < 0) {
        return NULL;
    }
    if (!socket_table[idx].used) {
        return NULL;
    }
    return &socket_table[idx];
}

static int socket_alloc_slot(pid_t owner_pid) {
    for (int i = 0; i < SOCKET_MAX; i++) {
        if (!socket_table[i].used) {
            memset(&socket_table[i], 0, sizeof(socket_table[i]));
            socket_table[i].used = true;
            socket_table[i].owner_pid = owner_pid;
            socket_table[i].tcp_conn_id = -1;
            socket_table[i].peer_fd = -1;
            return SOCKET_FD_BASE + i;
        }
    }
    return -EMFILE;
}

static size_t unix_socket_readable(const struct ksocket *sock) {
    return sock ? sock->rx_len : 0;
}

static size_t unix_socket_writable(const struct ksocket *sock) {
    if (!sock || sock->peer_fd < 0) {
        return 0;
    }
    struct ksocket *peer = socket_get_any(sock->peer_fd);
    if (!peer || !peer->used) {
        return 0;
    }
    if (peer->rx_len >= UNIX_RX_BUF) {
        return 0;
    }
    return UNIX_RX_BUF - peer->rx_len;
}

static int unix_socket_queue_pending(struct ksocket *listener, int fd) {
    uint8_t next = (uint8_t)((listener->pending_head + 1U) % UNIX_PENDING_MAX);
    if (next == listener->pending_tail) {
        return -EAGAIN;
    }
    listener->pending_fds[listener->pending_head] = fd;
    listener->pending_head = next;
    return 0;
}

static int unix_socket_pop_pending(struct ksocket *listener) {
    int fd;
    if (listener->pending_head == listener->pending_tail) {
        return -EAGAIN;
    }
    fd = listener->pending_fds[listener->pending_tail];
    listener->pending_tail = (uint8_t)((listener->pending_tail + 1U) % UNIX_PENDING_MAX);
    return fd;
}

static int unix_socket_rx_push(struct ksocket *sock, const uint8_t *data, size_t len) {
    if (!sock || !data) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }
    size_t space = UNIX_RX_BUF - sock->rx_len;
    if (space == 0) {
        return -EAGAIN;
    }
    size_t n = (len < space) ? len : space;
    size_t write_idx = (sock->rx_head + sock->rx_len) % UNIX_RX_BUF;
    for (size_t i = 0; i < n; i++) {
        sock->rx_buf[(write_idx + i) % UNIX_RX_BUF] = data[i];
    }
    sock->rx_len += n;
    return (int)n;
}

static int unix_socket_rx_pop(struct ksocket *sock, uint8_t *out, size_t len) {
    if (!sock || !out) {
        return -EINVAL;
    }
    if (sock->rx_len == 0) {
        return 0;
    }
    size_t n = (len < sock->rx_len) ? len : sock->rx_len;
    for (size_t i = 0; i < n; i++) {
        out[i] = sock->rx_buf[(sock->rx_head + i) % UNIX_RX_BUF];
    }
    sock->rx_head = (sock->rx_head + n) % UNIX_RX_BUF;
    sock->rx_len -= n;
    return (int)n;
}

/* Syscall statistics (for debugging/profiling) */
static uint64_t syscall_counts[NR_SYSCALLS];
/* Loader exec/trace debugging.
 * Disabled by default to keep interactive and command execution responsive. */
int loader_trace_enabled = 0;       /* 0=off, 1=on */
int loader_trace_budget = 0;       /* max syscall trace lines */
int loader_exec_debug_enabled = 1; /* extra exec/loader printk instrumentation */
static int tty_kd_mode = KD_TEXT;
static int tty_kb_mode = K_XLATE;

#define EXEC_USER_MAX_ARGC    256
#define EXEC_USER_MAX_STRLEN  4096

/*
 * Obelisk pathname / sysctl input policy: after copy_user_cstring succeeds, reject
 * empty strings, missing NUL within cap, C0 controls, and DEL (see zig_path.h).
 * Wired at: resolve_user_path_checked, resolve_user_at_path_checked, execve path,
 * readlinkat pathname, sysctl name.
 */
static int kernel_user_cstring_ok(const char *kstr, size_t cap) {
    if (!kstr || cap == 0) {
        return -EINVAL;
    }
    if (zig_kernel_cstring_no_control(kstr, (uint64_t)cap) != 0) {
        return -EINVAL;
    }
    return 0;
}

static int copy_user_cstring(const char *user_ptr, char *kernel_buf, size_t cap) {
    int64_t nul_at;

    if (!user_ptr || !kernel_buf || cap == 0) {
        return -EFAULT;
    }
    /*
     * One verified user->kernel copy (see vmm_copy_from_user), then find NUL.
     * Replaces per-byte copy (syscall hot path for paths, argv, env).
     */
    if (vmm_copy_from_user(kernel_buf, user_ptr, cap) < 0) {
        return -EFAULT;
    }
    nul_at = zig_cstring_first_nul_index(kernel_buf, (uint64_t)cap);
    if (nul_at < 0) {
        kernel_buf[cap - 1] = '\0';
        return -ENAMETOOLONG;
    }
    return 0;
}

static void free_exec_string_vector(char **vec) {
    if (!vec) {
        return;
    }
    for (size_t i = 0; i < EXEC_USER_MAX_ARGC && vec[i]; i++) {
        kfree(vec[i]);
    }
    kfree(vec);
}

static int copy_user_string_vector(char *const user_vec[], char ***kernel_vec_out) {
    char **kernel_vec;

    if (!kernel_vec_out) {
        return -EINVAL;
    }
    *kernel_vec_out = NULL;

    if (!user_vec) {
        return 0;
    }

    kernel_vec = kmalloc(sizeof(char *) * (EXEC_USER_MAX_ARGC + 1));
    if (!kernel_vec) {
        return -ENOMEM;
    }
    memset(kernel_vec, 0, sizeof(char *) * (EXEC_USER_MAX_ARGC + 1));

    for (size_t i = 0; i < EXEC_USER_MAX_ARGC; i++) {
        char *user_str = NULL;
        int ret = vmm_copy_from_user(&user_str,
                                     (const void *)((uintptr_t)user_vec + (i * sizeof(char *))),
                                     sizeof(user_str));
        if (ret < 0) {
            free_exec_string_vector(kernel_vec);
            return -EFAULT;
        }

        if (!user_str) {
            kernel_vec[i] = NULL;
            *kernel_vec_out = kernel_vec;
            return 0;
        }

        kernel_vec[i] = kmalloc(EXEC_USER_MAX_STRLEN);
        if (!kernel_vec[i]) {
            free_exec_string_vector(kernel_vec);
            return -ENOMEM;
        }
        ret = copy_user_cstring(user_str, kernel_vec[i], EXEC_USER_MAX_STRLEN);
        if (ret < 0) {
            free_exec_string_vector(kernel_vec);
            return ret;
        }
        if (zig_kernel_exec_line_ok(kernel_vec[i], (uint64_t)EXEC_USER_MAX_STRLEN) != 0) {
            free_exec_string_vector(kernel_vec);
            return -EINVAL;
        }
    }

    free_exec_string_vector(kernel_vec);
    return -E2BIG;
}

static int resolve_user_path_checked(const char *user_path, char *out, size_t out_size) {
    char kpath[PATH_MAX];
    int ret = copy_user_cstring(user_path, kpath, sizeof(kpath));
    if (ret < 0) {
        return ret;
    }
    ret = kernel_user_cstring_ok(kpath, sizeof(kpath));
    if (ret < 0) {
        return ret;
    }
    return resolve_user_path(kpath, out, out_size);
}

static int resolve_user_at_path_checked(int dirfd, const char *user_path, char *out, size_t out_size) {
    char kpath[PATH_MAX];
    int ret = copy_user_cstring(user_path, kpath, sizeof(kpath));
    if (ret < 0) {
        return ret;
    }
    ret = kernel_user_cstring_ok(kpath, sizeof(kpath));
    if (ret < 0) {
        return ret;
    }
    return resolve_user_at_path(dirfd, kpath, out, out_size);
}

/* Initialize syscall table */
static void syscall_table_init(void) {
    /* Clear table */
    for (int i = 0; i < NR_SYSCALLS; i++) {
        syscall_table[i] = NULL;
        syscall_counts[i] = 0;
    }
    memset(socket_table, 0, sizeof(socket_table));
    
    /* Register syscall handlers */
    syscall_table[SYS_READ] = (syscall_fn_t)sys_read;
    syscall_table[SYS_WRITE] = (syscall_fn_t)sys_write;
    syscall_table[SYS_WRITEV] = (syscall_fn_t)sys_writev;
    syscall_table[SYS_OPEN] = (syscall_fn_t)sys_open;
    syscall_table[SYS_CLOSE] = (syscall_fn_t)sys_close;
    syscall_table[SYS_STAT] = (syscall_fn_t)sys_stat;
    syscall_table[SYS_FSTAT] = (syscall_fn_t)sys_fstat;
    syscall_table[SYS_POLL] = (syscall_fn_t)sys_poll;
    syscall_table[SYS_ACCESS] = (syscall_fn_t)sys_access;
    syscall_table[SYS_LSEEK] = (syscall_fn_t)sys_lseek;
    syscall_table[17] = (syscall_fn_t)sys_pread64; /* pread64 */
    syscall_table[13] = (syscall_fn_t)sys_rt_sigaction;                 /* rt_sigaction */
    syscall_table[14] = (syscall_fn_t)sys_rt_sigprocmask;               /* rt_sigprocmask */
    syscall_table[SYS_MPROTECT] = (syscall_fn_t)sys_mprotect;
    syscall_table[SYS_MMAP] = (syscall_fn_t)sys_mmap;
    syscall_table[SYS_MUNMAP] = (syscall_fn_t)sys_munmap;
    syscall_table[SYS_BRK] = (syscall_fn_t)sys_brk;
    syscall_table[SYS_SCHED_YIELD] = (syscall_fn_t)sys_sched_yield;
    syscall_table[SYS_CLONE] = (syscall_fn_t)sys_clone;
    syscall_table[SYS_FORK] = (syscall_fn_t)sys_fork;
    syscall_table[SYS_EXECVE] = (syscall_fn_t)sys_execve;
    syscall_table[SYS_EXIT] = (syscall_fn_t)sys_exit;
    syscall_table[SYS_EXIT_GROUP] = (syscall_fn_t)sys_exit_group;
    syscall_table[SYS_WAIT4] = (syscall_fn_t)sys_wait4;
    syscall_table[SYS_GETPID] = (syscall_fn_t)sys_getpid;
    syscall_table[SYS_GETTID] = (syscall_fn_t)sys_gettid;
    syscall_table[SYS_SET_TID_ADDRESS] = (syscall_fn_t)sys_set_tid_address; /* 218 */
    syscall_table[273] = (syscall_fn_t)sys_set_robust_list;               /* 273 set_robust_list */
    syscall_table[SYS_CLOCK_GETTIME] = (syscall_fn_t)sys_clock_gettime;   /* 228 */
    syscall_table[302] = (syscall_fn_t)sys_prlimit64;                     /* 302 prlimit64 */
    syscall_table[318] = (syscall_fn_t)sys_getrandom;                      /* 318 getrandom */
    syscall_table[334] = (syscall_fn_t)sys_rseq;                           /* 334 rseq */
    syscall_table[SYS_PRCTL] = (syscall_fn_t)sys_prctl;                    /* 157 prctl */
    syscall_table[SYS_GETPPID] = (syscall_fn_t)sys_getppid;
    syscall_table[SYS_GETUID] = (syscall_fn_t)sys_getuid;
    syscall_table[SYS_GETGID] = (syscall_fn_t)sys_getgid;
    syscall_table[SYS_GETEUID] = (syscall_fn_t)sys_geteuid;
    syscall_table[SYS_GETEGID] = (syscall_fn_t)sys_getegid;
    syscall_table[SYS_SETUID] = (syscall_fn_t)sys_setuid;
    syscall_table[SYS_SETGID] = (syscall_fn_t)sys_setgid;
    syscall_table[SYS_UMASK] = (syscall_fn_t)sys_umask;
    syscall_table[SYS_SETPGID] = (syscall_fn_t)sys_setpgid;
    syscall_table[SYS_GETPGRP] = (syscall_fn_t)sys_getpgrp;
    syscall_table[SYS_SETSID] = (syscall_fn_t)sys_setsid;
    syscall_table[SYS_SETREUID] = (syscall_fn_t)sys_setreuid;
    syscall_table[SYS_SETREGID] = (syscall_fn_t)sys_setregid;
    syscall_table[SYS_SETRESUID] = (syscall_fn_t)sys_setresuid;
    syscall_table[SYS_SETRESGID] = (syscall_fn_t)sys_setresgid;
    syscall_table[SYS_GETPGID] = (syscall_fn_t)sys_getpgid;
    syscall_table[SYS_GETSID] = (syscall_fn_t)sys_getsid;
    syscall_table[SYS_SETFSUID] = (syscall_fn_t)sys_setfsuid;
    syscall_table[SYS_SETFSGID] = (syscall_fn_t)sys_setfsgid;
    syscall_table[SYS_CHDIR] = (syscall_fn_t)sys_chdir;
    syscall_table[SYS_GETCWD] = (syscall_fn_t)sys_getcwd;
    syscall_table[SYS_MKDIR] = (syscall_fn_t)sys_mkdir;
    syscall_table[SYS_RMDIR] = (syscall_fn_t)sys_rmdir;
    syscall_table[SYS_UNLINK] = (syscall_fn_t)sys_unlink;
    syscall_table[SYS_CHMOD] = (syscall_fn_t)sys_chmod;
    syscall_table[SYS_CHOWN] = (syscall_fn_t)sys_chown;
    syscall_table[SYS_LINK] = (syscall_fn_t)sys_link;
    syscall_table[SYS_RENAME] = (syscall_fn_t)sys_rename;
    syscall_table[SYS_OPENAT] = (syscall_fn_t)sys_openat;
    syscall_table[SYS_NEWFSTATAT] = (syscall_fn_t)sys_newfstatat;
    syscall_table[SYS_READLINKAT] = (syscall_fn_t)sys_readlinkat;
    syscall_table[SYS_FACCESSAT] = (syscall_fn_t)sys_faccessat;
    syscall_table[SYS_DUP] = (syscall_fn_t)sys_dup;
    syscall_table[SYS_DUP2] = (syscall_fn_t)sys_dup2;
    syscall_table[SYS_PIPE] = (syscall_fn_t)sys_pipe;
    syscall_table[SYS_SELECT] = (syscall_fn_t)sys_select;
    syscall_table[SYS_NANOSLEEP] = (syscall_fn_t)sys_nanosleep;
    syscall_table[SYS_IOCTL] = (syscall_fn_t)sys_ioctl;
    syscall_table[SYS_FCNTL] = (syscall_fn_t)sys_fcntl;
    syscall_table[SYS_GETDENTS64] = (syscall_fn_t)sys_getdents64;
    syscall_table[SYS_KILL] = (syscall_fn_t)sys_kill;
    syscall_table[SYS_UNAME] = (syscall_fn_t)sys_uname;
    syscall_table[SYS_STATFS] = (syscall_fn_t)sys_statfs;
    syscall_table[SYS_MOUNT] = (syscall_fn_t)sys_mount;
    syscall_table[SYS_UMOUNT2] = (syscall_fn_t)sys_umount2;
    syscall_table[SYS_REBOOT] = (syscall_fn_t)sys_reboot;
    syscall_table[SYS_OBELISK_SYSCTL] = (syscall_fn_t)sys_sysctl;
    syscall_table[SYS_OBELISK_PROC_LIST] = (syscall_fn_t)sys_obelisk_proc_list;
    /*
     * Networking ABI placeholders:
     * syscalls are explicitly wired so userspace gets deterministic
     * "network stack unavailable" errors instead of generic ENOSYS.
     */
    syscall_table[SYS_SOCKET] = (syscall_fn_t)sys_socket;
    syscall_table[SYS_CONNECT] = (syscall_fn_t)sys_connect;
    syscall_table[SYS_ACCEPT] = (syscall_fn_t)sys_accept;
    syscall_table[SYS_SOCKETPAIR] = (syscall_fn_t)sys_socketpair;
    syscall_table[SYS_SENDTO] = (syscall_fn_t)sys_sendto;
    syscall_table[SYS_RECVFROM] = (syscall_fn_t)sys_recvfrom;
    syscall_table[SYS_BIND] = (syscall_fn_t)sys_bind;
    syscall_table[SYS_LISTEN] = (syscall_fn_t)sys_listen;
    syscall_table[SYS_SHUTDOWN] = (syscall_fn_t)sys_shutdown;
    syscall_table[SYS_PPOLL] = (syscall_fn_t)sys_poll;
    syscall_table[SYS_PSELECT6] = (syscall_fn_t)sys_select;
    syscall_table[SYS_ARCH_PRCTL] = (syscall_fn_t)sys_arch_prctl;
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
    /* Minimal compat:
     * - AT_FDCWD and absolute paths resolve like normal paths.
     * - For loader compatibility, also support dirfd-relative paths by
     *   resolving against the directory referenced by dirfd.
     */
    if (dirfd == -100 || (path && path[0] == '/')) {
        return resolve_user_path(path, out, out_size);
    }

    struct process *proc = current;
    if (!proc || !proc->files) {
        return -EBADF;
    }
    if (dirfd < 0) {
        return -ENOSYS;
    }

    struct file *df = fd_get(proc->files, dirfd);
    if (!df || !df->f_dentry || !df->f_dentry->d_inode) {
        return -EBADF;
    }

    /* Convert dirfd's dentry to an absolute path under proc->root. */
    char base[PATH_MAX];
    {
        char tmp[PATH_MAX];
        size_t pos = PATH_MAX - 1;
        tmp[pos] = '\0';

        if (df->f_dentry == proc->root || df->f_dentry->d_parent == NULL) {
            /* Root. */
            tmp[0] = '/';
            tmp[1] = '\0';
            strncpy(base, tmp, sizeof(base) - 1);
            base[sizeof(base) - 1] = '\0';
        } else {
            struct dentry *d = df->f_dentry;
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
            /* Copy final into base. */
            size_t len = (PATH_MAX - 1) - pos;
            if (len + 1 > sizeof(base)) {
                return -ERANGE;
            }
            memcpy(base, &tmp[pos], len + 1);
        }
    }

    if (!path || path[0] == '\0') {
        /* fstatat(dirfd, "", ...) -> just resolve base */
        if (snprintf(out, out_size, "%s", base) >= (int)out_size) {
            return -ENAMETOOLONG;
        }
        return 0;
    }

    if (strcmp(base, "/") == 0) {
        return resolve_user_path(path[0] == '/' ? path : "/", out, out_size); /* shouldn't happen for rel path */
    }

    /* Join base + "/" + path. */
    if (snprintf(out, out_size, "%s/%s", base, path) >= (int)out_size) {
        return -ENAMETOOLONG;
    }
    return 0;
}

static void apply_input_path_alias(char *path, size_t cap) {
    if (!path || cap == 0) {
        return;
    }
    if (strcmp(path, "/dev/input/mice") == 0) {
        strncpy(path, "/dev/mice", cap - 1);
        path[cap - 1] = '\0';
        return;
    }
    if (strcmp(path, "/dev/input/event0") == 0) {
        strncpy(path, "/dev/event0", cap - 1);
        path[cap - 1] = '\0';
        return;
    }
    if (strcmp(path, "/dev/input/event1") == 0) {
        strncpy(path, "/dev/event1", cap - 1);
        path[cap - 1] = '\0';
        return;
    }
    if (strcmp(path, "/dev/input/mouse0") == 0) {
        strncpy(path, "/dev/mice", cap - 1);
        path[cap - 1] = '\0';
    }
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
    if (loader_trace_enabled != 0 && loader_trace_budget > 0 && current) {
        const char *comm = current->comm;
        const bool is_loader_proc =
            (comm && (strcmp(comm, "xinit") == 0 || strcmp(comm, "xdm") == 0 ||
                      strncmp(comm, "ld-linux", 8) == 0));
        if (is_loader_proc && (syscall_num == SYS_EXECVE || syscall_num == SYS_ARCH_PRCTL)) {
            trace_loader = 1;
            loader_trace_budget--;
            printk(KERN_INFO
                   "syscall-trace: comm=%s nr=%lu a1=0x%lx a2=0x%lx a3=0x%lx a4=0x%lx\n",
                   comm ? comm : "?", syscall_num, arg1, arg2, arg3, arg4);
        }
    }

    current_syscall_regs = regs;
    /* Call handler */
    int64_t ret = handler(arg1, arg2, arg3, arg4, arg5, arg6);
    current_syscall_regs = NULL;
    if (trace_loader) {
        printk(KERN_INFO "syscall-trace: nr=%lu ret=%ld\n", syscall_num, ret);
    }
    return ret;
}

/* ==========================================================================
 * Syscall implementations (stubs for now)
 * ========================================================================== */

static int maybe_handle_ctrlc(void) {
    struct process *proc = current;
    if (!proc) {
        return 0;
    }

    int c = devfs_console_peekc_nonblock();
    if (c != 0x03) {
        return 0;
    }

    /* Mirror sys_read() Ctrl+C behavior. */
    console_write("^C\n", 3);
    (void)devfs_console_getc_nonblock();
    devfs_console_flush_input();

    /* For the in-process shell (rockbox/osh applets), just interrupt syscalls.
     * Many builtins (e.g. `cat`) explicitly exit on EINTR. */
    if (strcmp(proc->comm, "rockbox") != 0 && strcmp(proc->comm, "osh") != 0) {
        (void)do_kill(proc->pid, SIGINT);
    }

    return -EINTR;
}

static int64_t sys_read(int fd, void *buf, size_t count) {
    struct process *proc = current;
    struct file *file = NULL;
    struct ksocket *sock = NULL;
    uint8_t kbuf[256];
    size_t done = 0;

    if (!buf) {
        return -EFAULT;
    }

    sock = socket_get_owned(fd);
    if (sock) {
        return sys_recvfrom(fd, buf, count, 0, NULL, NULL);
    }

    if (proc && proc->files && fd >= 0 && fd < (int)proc->files->max_fds) {
        file = fd_get(proc->files, fd);
    }
    if (file) {
        unsigned ctrlc_check = 0;
        while (done < count) {
            size_t chunk = count - done;
            if (chunk > sizeof(kbuf)) {
                chunk = sizeof(kbuf);
            }

            /* Keep Ctrl-C responsive even for relatively large user read buffers. */
            if (((ctrlc_check++) & 7U) == 0U) {
                int rc = maybe_handle_ctrlc();
                if (rc) {
                    return rc;
                }
            }

            int64_t n = vfs_read(file, kbuf, chunk, NULL);
            if (n < 0) {
                return (done > 0) ? (int64_t)done : n;
            }
            if (n == 0) {
                break;
            }
            if (vmm_copy_to_user((uint8_t *)buf + done, kbuf, (size_t)n) < 0) {
                return (done > 0) ? (int64_t)done : -EFAULT;
            }
            done += (size_t)n;
            if ((size_t)n < chunk) {
                break;
            }
        }
        return (int64_t)done;
    }
    if (fd != 0) {
        return -EBADF;
    }
    if (count == 0) {
        return 0;
    }

    /* Character mode for interactive shells/editors. */
    if (count == 1) {
        char c = devfs_console_getc();
        if (c == '\r') {
            c = '\n';
        }
        if (c == 0x03) {
            console_write("^C\n", 3);
            tty_flush_input();
            return -EINTR;
        }
        if (vmm_copy_to_user(buf, &c, 1) < 0) {
            return -EFAULT;
        }
        return 1;
    }

    size_t i = 0;
    while (i < count) {
        char c = devfs_console_getc();
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

        if (vmm_copy_to_user((uint8_t *)buf + i, &c, 1) < 0) {
            return (i > 0) ? (int64_t)i : -EFAULT;
        }
        i++;
        console_putc(c);

        if (c == '\n') {
            break;
        }
    }

    return (int64_t)i;
}

static int64_t sys_pread64(int fd, void *buf, size_t count, loff_t offset) {
    struct process *proc = current;
    struct file *file = NULL;
    uint8_t kbuf[256];
    size_t done = 0;

    if (offset < 0) {
        return -EINVAL;
    }
    if (!buf && count != 0) {
        return -EFAULT;
    }
    if (count != 0) {
        uint64_t sum_end;
        if (zig_u64_add_ok((uint64_t)offset, (uint64_t)count, &sum_end) != 0) {
            return -EOVERFLOW;
        }
    }

    if (proc && proc->files && fd >= 0 && fd < (int)proc->files->max_fds) {
        file = fd_get(proc->files, fd);
    }
    if (file) {
        loff_t pos = offset;
        while (done < count) {
            size_t chunk = count - done;
            if (chunk > sizeof(kbuf)) {
                chunk = sizeof(kbuf);
            }

            int64_t n = vfs_read(file, kbuf, chunk, &pos);
            if (n < 0) {
                return (done > 0) ? (int64_t)done : n;
            }
            if (n == 0) {
                break;
            }
            if (vmm_copy_to_user((uint8_t *)buf + done, kbuf, (size_t)n) < 0) {
                return (done > 0) ? (int64_t)done : -EFAULT;
            }
            done += (size_t)n;
            if ((size_t)n < chunk) {
                break;
            }
        }
        return (int64_t)done;
    }

    if (fd != 0) {
        return -EBADF;
    }

    return -EBADF;
}

static int64_t sys_write(int fd, const void *buf, size_t count) {
    struct process *proc = current;
    struct file *file = NULL;
    struct ksocket *sock = NULL;
    uint8_t kbuf[256];
    size_t done = 0;

    /* POSIX/Linux: write(fd, NULL, 0) returns 0 without faulting. */
    if (!buf && count != 0) {
        return -EFAULT;
    }

    sock = socket_get_owned(fd);
    if (sock) {
        if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
            return sys_sendto(fd, buf, count, 0, NULL, 0);
        }
        if (sock->type == SOCK_STREAM && sock->connected) {
            return sys_sendto(fd, buf, count, 0, NULL, 0);
        }
        return -EDESTADDRREQ;
    }

    if (proc && proc->files && fd >= 0 && fd < (int)proc->files->max_fds) {
        file = fd_get(proc->files, fd);
    }
    if (file) {
        while (done < count) {
            size_t chunk = count - done;
            if (chunk > sizeof(kbuf)) {
                chunk = sizeof(kbuf);
            }
            if (vmm_copy_from_user(kbuf, (const uint8_t *)buf + done, chunk) < 0) {
                return (done > 0) ? (int64_t)done : -EFAULT;
            }
            int64_t n = vfs_write(file, kbuf, chunk, NULL);
            if (n < 0) {
                return (done > 0) ? (int64_t)done : n;
            }
            done += (size_t)n;
            if ((size_t)n < chunk) {
                break;
            }
        }
        return (int64_t)done;
    }

    /* Mirror userspace output to serial and VGA for default stdio. */
    if (fd == 1 || fd == 2) {
        unsigned ctrlc_check = 0;
        while (done < count) {
            size_t chunk = count - done;
            if (chunk > sizeof(kbuf)) {
                chunk = sizeof(kbuf);
            }

            if (((ctrlc_check++) & 7U) == 0U) {
                int rc = maybe_handle_ctrlc();
                if (rc) {
                    return rc;
                }
            }

            if (vmm_copy_from_user(kbuf, (const uint8_t *)buf + done, chunk) < 0) {
                return (done > 0) ? (int64_t)done : -EFAULT;
            }
            console_write((const char *)kbuf, chunk);
            done += chunk;
        }
        return (int64_t)done;
    }
    return -EBADF;
}

static int64_t sys_writev(int fd, const void *iov, int iovcnt) {
    const struct iovec *uvec = (const struct iovec *)iov;
    struct iovec kv;
    int64_t total = 0;
    uint64_t iov_sum = 0;

    if (!uvec || iovcnt < 0) {
        return -EINVAL;
    }
    if (iovcnt > 1024) {
        return -EINVAL;
    }

    for (int i = 0; i < iovcnt; i++) {
        if (vmm_copy_from_user(&kv, uvec + i, sizeof(kv)) < 0) {
            return (total > 0) ? total : -EFAULT;
        }
        if (!kv.iov_base && kv.iov_len != 0) {
            return -EFAULT;
        }
        if (kv.iov_len == 0) {
            continue;
        }
        if (zig_u64_add_ok(iov_sum, (uint64_t)kv.iov_len, &iov_sum) != 0) {
            return -EINVAL;
        }
        if (zig_user_copy_len_ok(iov_sum) != 0) {
            return -EINVAL;
        }
        int64_t n = sys_write(fd, kv.iov_base, kv.iov_len);
        if (n < 0) {
            return (total > 0) ? total : n;
        }
        total += n;
        if ((size_t)n < kv.iov_len) {
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
    ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    apply_input_path_alias(resolved, sizeof(resolved));

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
    struct ksocket *sock = socket_get_owned(fd);
    if (sock) {
        if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM && sock->peer_fd >= 0) {
            struct ksocket *peer = socket_get_any(sock->peer_fd);
            if (peer && peer->used) {
                peer->peer_closed = true;
                if (peer->peer_fd == fd) {
                    peer->peer_fd = -1;
                    peer->connected = false;
                }
            }
        }
        if (sock->type == SOCK_STREAM && sock->protocol == IPPROTO_TCP && sock->tcp_conn_id >= 0) {
            (void)net_tcp_close(sock->tcp_conn_id);
        }
        memset(sock, 0, sizeof(*sock));
        return 0;
    }
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
    struct stat ust;
    char resolved[PATH_MAX];
    int ret;
    if (!statbuf) {
        return -EFAULT;
    }
    ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    ret = vfs_stat(resolved, &kst);
    if (ret < 0) {
        return ret;
    }
    kstat_to_uapi_stat(&kst, &ust);
    if (vmm_copy_to_user(statbuf, &ust, sizeof(ust)) < 0) {
        return -EFAULT;
    }
    return 0;
}

static int64_t sys_fstat(int fd, struct stat *statbuf) {
    struct process *proc = current;
    struct file *file;
    struct kstat kst;
    struct stat ust;
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
    kstat_to_uapi_stat(&kst, &ust);
    if (vmm_copy_to_user(statbuf, &ust, sizeof(ust)) < 0) {
        return -EFAULT;
    }
    return 0;
}

static int64_t sys_statfs(const char *pathname, void *buf) {
    char resolved[PATH_MAX];
    struct dentry *dentry;
    struct statfs_compat out;
    int ret;

    if (!buf) {
        return -EFAULT;
    }
    ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    dentry = vfs_lookup(resolved);
    if (!dentry) {
        return -ENOENT;
    }

    memset(&out, 0, sizeof(out));
    if (dentry->d_sb) {
        out.f_type = (int64_t)dentry->d_sb->s_magic;
        out.f_bsize = (int64_t)(dentry->d_sb->s_blocksize ? dentry->d_sb->s_blocksize : PAGE_SIZE);
        out.f_frsize = out.f_bsize;
    } else {
        out.f_bsize = PAGE_SIZE;
        out.f_frsize = PAGE_SIZE;
    }
    out.f_namelen = NAME_MAX;

    dput(dentry);
    if (vmm_copy_to_user(buf, &out, sizeof(out)) < 0) {
        return -EFAULT;
    }
    return 0;
}

static bool syscall_require_fsuid_root(void) {
    struct process *p = current;
    return p && p->cred && p->cred->fsuid == 0;
}

static int64_t sys_mount(char *source, char *target, char *fstype, unsigned long flags, void *data) {
    char ksrc[PATH_MAX];
    char ktgt[PATH_MAX];
    char kfs[64];
    const char *psrc = NULL;
    int ret;

    (void)data;
    if (!syscall_require_fsuid_root()) {
        return -EPERM;
    }
    if (!target || !fstype) {
        return -EINVAL;
    }
    memset(ksrc, 0, sizeof(ksrc));
    memset(ktgt, 0, sizeof(ktgt));
    memset(kfs, 0, sizeof(kfs));
    if (source) {
        ret = copy_user_cstring(source, ksrc, sizeof(ksrc));
        if (ret < 0) {
            return ret;
        }
        ret = kernel_user_cstring_ok(ksrc, sizeof(ksrc));
        if (ret < 0) {
            return ret;
        }
        psrc = ksrc;
    }
    ret = copy_user_cstring(target, ktgt, sizeof(ktgt));
    if (ret < 0) {
        return ret;
    }
    ret = kernel_user_cstring_ok(ktgt, sizeof(ktgt));
    if (ret < 0) {
        return ret;
    }
    ret = copy_user_cstring(fstype, kfs, sizeof(kfs));
    if (ret < 0) {
        return ret;
    }
    ret = kernel_user_cstring_ok(kfs, sizeof(kfs));
    if (ret < 0) {
        return ret;
    }
    return vfs_mount(psrc, ktgt, kfs, flags, NULL);
}

static int64_t sys_umount2(const char *target, int flags) {
    char ktgt[PATH_MAX];
    int ret;

    if (!syscall_require_fsuid_root()) {
        return -EPERM;
    }
    if (!target) {
        return -EINVAL;
    }
    ret = copy_user_cstring(target, ktgt, sizeof(ktgt));
    if (ret < 0) {
        return ret;
    }
    ret = kernel_user_cstring_ok(ktgt, sizeof(ktgt));
    if (ret < 0) {
        return ret;
    }
    return vfs_umount(ktgt, flags);
}

static int64_t sys_obelisk_proc_list(void *ubuf, size_t *ulenp) {
    size_t cap;
    size_t out;
    char *kbuf;
    int ret;

    if (!ubuf || !ulenp) {
        return -EFAULT;
    }
    if (vmm_copy_from_user(&cap, ulenp, sizeof(cap)) < 0) {
        return -EFAULT;
    }
    if (cap < 64 || cap > 65536) {
        return -EINVAL;
    }
    kbuf = kmalloc(cap);
    if (!kbuf) {
        return -ENOMEM;
    }
    out = cap;
    ret = sysctl_read("system.proc.list", kbuf, &out);
    if (ret < 0) {
        kfree(kbuf);
        return ret;
    }
    if (vmm_copy_to_user(ubuf, kbuf, out) < 0) {
        kfree(kbuf);
        return -EFAULT;
    }
    if (vmm_copy_to_user(ulenp, &out, sizeof(out)) < 0) {
        kfree(kbuf);
        return -EFAULT;
    }
    kfree(kbuf);
    return 0;
}

static int64_t sys_access(const char *pathname, int mode) {
    char resolved[PATH_MAX];
    struct dentry *dentry;
    int ret;
    int mask = 0;

    ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    dentry = vfs_lookup(resolved);
    if (!dentry || !dentry->d_inode) {
        if (dentry) {
            dput(dentry);
        }
        return -ENOENT;
    }
    /* F_OK (0): existence only. R_OK/W_OK/X_OK match Linux bitmask → MAY_* in inode.h */
    if (mode == F_OK) {
        dput(dentry);
        return 0;
    }
    if (mode & R_OK) {
        mask |= MAY_READ;
    }
    if (mode & W_OK) {
        mask |= MAY_WRITE;
    }
    if (mode & X_OK) {
        mask |= MAY_EXEC;
    }
    ret = generic_permission(dentry->d_inode, mask);
    dput(dentry);
    return ret;
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
        if (((uint64_t)offset & (PAGE_SIZE - 1)) != 0) {
            return -EINVAL;
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

static int64_t sys_rt_sigaction(int signum, const void *act, void *oldact, size_t sigsetsize) {
    struct process *proc = current;
    struct kernel_sigaction_compat uact;
    struct kernel_sigaction_compat uold;
    struct sigaction *slot;

    if (!proc) {
        return -ESRCH;
    }
    if (sigsetsize != sizeof(uint64_t)) {
        return -EINVAL;
    }
    if (signum <= 0 || signum >= NSIG) {
        return -EINVAL;
    }

    slot = &proc->sigactions[signum];

    if (oldact) {
        memset(&uold, 0, sizeof(uold));
        uold.sa_handler = (uint64_t)(uintptr_t)slot->sa_handler;
        uold.sa_flags = (uint64_t)slot->sa_flags;
        uold.sa_restorer = (uint64_t)(uintptr_t)slot->sa_restorer;
        uold.sa_mask = (uint64_t)slot->sa_mask;
        if (vmm_copy_to_user(oldact, &uold, sizeof(uold)) < 0) {
            return -EFAULT;
        }
    }

    if (act) {
        if (vmm_copy_from_user(&uact, act, sizeof(uact)) < 0) {
            return -EFAULT;
        }
        slot->sa_handler = (sighandler_t)(uintptr_t)uact.sa_handler;
        slot->sa_flags = (int)uact.sa_flags;
        slot->sa_restorer = (void (*)(void))(uintptr_t)uact.sa_restorer;
        slot->sa_mask = (sigset_t)uact.sa_mask;
    }

    return 0;
}

static int64_t sys_rt_sigprocmask(int how, const void *set, void *oldset, size_t sigsetsize) {
    struct process *proc = current;
    sigset_t newset = 0;

    if (!proc) {
        return -ESRCH;
    }
    if (sigsetsize != sizeof(uint64_t)) {
        return -EINVAL;
    }

    if (oldset) {
        sigset_t old = proc->blocked;
        if (vmm_copy_to_user(oldset, &old, sizeof(old)) < 0) {
            return -EFAULT;
        }
    }

    if (!set) {
        return 0;
    }
    if (vmm_copy_from_user(&newset, set, sizeof(newset)) < 0) {
        return -EFAULT;
    }

    switch (how) {
        case SIG_BLOCK_MASK:
            proc->blocked |= newset;
            break;
        case SIG_UNBLOCK_MASK:
            proc->blocked &= ~newset;
            break;
        case SIG_SETMASK_MASK:
            proc->blocked = newset;
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

static int64_t sys_clone(uint64_t flags, uint64_t child_stack, void *parent_tid, void *child_tid, uint64_t tls) {
    if (!current_syscall_regs) {
        return -EINVAL;
    }

    /* Keep semantics conservative: support process-style clone, not threads yet. */
    if (flags & (CLONE_THREAD | CLONE_SETTLS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) {
        return -ENOSYS;
    }

    int64_t child = do_fork((uint32_t)flags, child_stack, current_syscall_regs);
    if (child < 0) {
        return child;
    }

    if ((flags & CLONE_PARENT_SETTID) && parent_tid) {
        int32_t ctid = (int32_t)child;
        if (vmm_copy_to_user(parent_tid, &ctid, sizeof(ctid)) < 0) {
            return -EFAULT;
        }
    }

    (void)child_tid;
    (void)tls;
    return child;
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
    char *kpath = NULL;
    char **kargv = NULL;
    char **kenvp = NULL;
    int64_t ret;

    if (!pathname) {
        return -EFAULT;
    }

    kpath = kmalloc(PATH_MAX);
    if (!kpath) {
        return -ENOMEM;
    }
    ret = copy_user_cstring(pathname, kpath, PATH_MAX);
    if (ret < 0) {
        kfree(kpath);
        return ret;
    }
    ret = kernel_user_cstring_ok(kpath, PATH_MAX);
    if (ret < 0) {
        kfree(kpath);
        return ret;
    }

    ret = copy_user_string_vector(argv, &kargv);
    if (ret < 0) {
        kfree(kpath);
        return ret;
    }

    ret = copy_user_string_vector(envp, &kenvp);
    if (ret < 0) {
        free_exec_string_vector(kargv);
        kfree(kpath);
        return ret;
    }

    ret = do_execve(kpath, kargv, kenvp);

    /* Success path does not return. */
    free_exec_string_vector(kenvp);
    free_exec_string_vector(kargv);
    kfree(kpath);
    return ret;
}

static int64_t sys_exit(int status) {
    struct process *p = current;
    if (p && p->pid == 2) {
        static char *const rescue_argv[] = { "osh", "-i", NULL };
        static char *const rescue_envp[] = {
            "PATH=/bin:/sbin:/usr/bin",
            "HOME=/",
            "SHELL=/bin/osh",
            "TERM=vt100",
            NULL
        };
        printk(KERN_ERR "init: pid1 attempted exit(%d), starting rescue shell\n", status);
        if (do_execve("/bin/osh", rescue_argv, rescue_envp) >= 0) {
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
    int kstatus = 0;
    int64_t ret;
    (void)rusage;
    ret = do_wait(pid, wstatus ? &kstatus : NULL, options);
    if (ret < 0 || !wstatus) {
        return ret;
    }
    if (vmm_copy_to_user(wstatus, &kstatus, sizeof(kstatus)) < 0) {
        return -EFAULT;
    }
    return ret;
}

static int64_t sys_getpid(void) {
    struct process *p = current;
    return p ? p->pid : 0;
}

static int64_t sys_gettid(void) {
    struct process *p = current;
    return p ? p->pid : 0;
}

/* Linux x86_64 syscall #218: set_tid_address */
static int64_t sys_set_tid_address(uint64_t tidptr) {
    struct process *p = current;
    if (!tidptr) {
        return -EINVAL;
    }
    int tid = p ? (int)p->pid : 0;
    if (vmm_copy_to_user((void *)tidptr, &tid, sizeof(tid)) < 0) {
        return -EFAULT;
    }
    /* Linux ABI: return 0 on success. */
    return 0;
}

/* Linux x86_64 syscall #273: set_robust_list (single-thread stub) */
static int64_t sys_set_robust_list(uint64_t head, uint64_t len) {
    (void)head;
    (void)len;
    return 0;
}

/* Linux x86_64 syscall #302: prlimit64 (conservative defaults) */
static int64_t sys_prlimit64(uint64_t pid, uint64_t new_limit, uint64_t old_limit, uint64_t flags) {
    (void)pid;
    (void)new_limit;
    (void)flags;

    if (!old_limit) {
        return 0;
    }

    /* Conservative RLIMIT_STACK approximation to avoid "infinite" stacks. */
    struct rlimit_compat rl = {
        .rlim_cur = (uint64_t)CONFIG_USER_STACK_SIZE,
        .rlim_max = (uint64_t)CONFIG_USER_STACK_SIZE,
    };
    if (vmm_copy_to_user((void *)old_limit, &rl, sizeof(rl)) < 0) {
        return -EFAULT;
    }
    return 0;
}

/* Linux x86_64 syscall #334: rseq (not supported yet) */
static int64_t sys_rseq(void *rseq, uint64_t rseq_len, uint64_t flags) {
    (void)rseq;
    (void)rseq_len;
    (void)flags;
    /* Minimal compatibility: treat rseq registration as supported. */
    if (!rseq) {
        return -EINVAL;
    }
    /* glibc passes a small user struct; require at least a header-ish size. */
    if (rseq_len < 0x10) {
        return -EINVAL;
    }
    if (flags != 0) {
        return -EINVAL;
    }
    return 0;
}

static int64_t sys_getppid(void) {
    struct process *p = current;
    if (!p || !p->parent) return 0;
    return p->parent->pid;
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

static int64_t sys_umask(mode_t mask) {
    struct process *p = current;
    mode_t old = 0022;
    if (!p) {
        return old;
    }
    old = p->umask;
    p->umask = (mask & 0777);
    return old;
}

static int64_t sys_setpgid(pid_t pid, pid_t pgid) {
    struct process *caller = current;
    struct process *target;
    if (!caller) {
        return -ESRCH;
    }
    if (pid == 0) {
        target = caller;
    } else {
        target = process_find(pid);
    }
    if (!target) {
        return -ESRCH;
    }
    if (target != caller) {
        return -EPERM;
    }
    if (pgid == 0) {
        pgid = target->pid;
    }
    if (pgid < 0) {
        return -EINVAL;
    }
    target->pgid = pgid;
    return 0;
}

static int64_t sys_getpgrp(void) {
    struct process *p = current;
    if (!p) {
        return 0;
    }
    return p->pgid;
}

static int64_t sys_setsid(void) {
    struct process *p = current;
    if (!p) {
        return -ESRCH;
    }
    if (p->pid == p->pgid) {
        return -EPERM;
    }
    p->sid = p->pid;
    p->pgid = p->pid;
    return p->sid;
}

static int64_t sys_getpgid(pid_t pid) {
    struct process *p;
    if (pid == 0) {
        p = current;
    } else {
        p = process_find(pid);
    }
    if (!p) {
        return -ESRCH;
    }
    return p->pgid;
}

static int64_t sys_getsid(pid_t pid) {
    struct process *p;
    if (pid == 0) {
        p = current;
    } else {
        p = process_find(pid);
    }
    if (!p) {
        return -ESRCH;
    }
    return p->sid;
}

static int64_t sys_chdir(const char *path) {
    struct process *proc = current;
    char resolved[PATH_MAX];
    struct dentry *dentry;
    int ret;

    if (!proc) {
        return -ESRCH;
    }
    ret = resolve_user_path_checked(path, resolved, sizeof(resolved));
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
    if (dentry->d_inode->i_op && dentry->d_inode->i_op->permission) {
        ret = dentry->d_inode->i_op->permission(dentry->d_inode, MAY_EXEC);
    } else {
        ret = generic_permission(dentry->d_inode, MAY_EXEC);
    }
    if (ret < 0) {
        dput(dentry);
        return ret;
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
    if (vmm_copy_to_user(buf, cwd, (size_t)(ret + 1)) < 0) {
        return -EFAULT;
    }
    return ret + 1;
}

static int64_t sys_mkdir(const char *pathname, mode_t mode) {
    char resolved[PATH_MAX];
    int ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    return vfs_mkdir(resolved, mode);
}

static int64_t sys_rmdir(const char *pathname) {
    char resolved[PATH_MAX];
    int ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    return vfs_rmdir(resolved);
}

static int64_t sys_unlink(const char *pathname) {
    char resolved[PATH_MAX];
    int ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    return vfs_unlink(resolved);
}

static int64_t sys_chmod(const char *pathname, mode_t mode) {
    char resolved[PATH_MAX];
    struct dentry *dentry;
    struct inode *inode;
    struct process *proc = current;
    uid_t fsuid;
    int ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }

    dentry = vfs_lookup(resolved);
    if (!dentry || !dentry->d_inode) {
        if (dentry) dput(dentry);
        return -ENOENT;
    }

    inode = dentry->d_inode;
    fsuid = (proc && proc->cred) ? proc->cred->fsuid : 0;
    if (fsuid != 0 && fsuid != inode->i_uid) {
        dput(dentry);
        return -EPERM;
    }

    inode->i_mode = (inode->i_mode & S_IFMT) | (mode & 07777);
    mark_inode_dirty(inode);
    dput(dentry);
    return 0;
}

static int64_t sys_chown(const char *pathname, uid_t owner, gid_t group) {
    char resolved[PATH_MAX];
    struct dentry *dentry;
    struct inode *inode;
    struct process *proc = current;
    uid_t fsuid = (proc && proc->cred) ? proc->cred->fsuid : 0;
    int ret = resolve_user_path_checked(pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }

    if (fsuid != 0) {
        return -EPERM;
    }

    dentry = vfs_lookup(resolved);
    if (!dentry || !dentry->d_inode) {
        if (dentry) dput(dentry);
        return -ENOENT;
    }

    inode = dentry->d_inode;
    inode->i_uid = owner;
    inode->i_gid = group;
    mark_inode_dirty(inode);
    dput(dentry);
    return 0;
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
    ret = resolve_user_at_path_checked(dirfd, pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    apply_input_path_alias(resolved, sizeof(resolved));

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
    struct stat ust;
    char resolved[PATH_MAX];
    int ret;
    (void)flags;
    if (!statbuf) {
        return -EFAULT;
    }
    ret = resolve_user_at_path_checked(dirfd, pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    ret = vfs_stat(resolved, &kst);
    if (ret < 0) {
        return ret;
    }
    kstat_to_uapi_stat(&kst, &ust);
    if (vmm_copy_to_user(statbuf, &ust, sizeof(ust)) < 0) {
        return -EFAULT;
    }
    return 0;
}

static int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    const char *target = NULL;
    char kpath[PATH_MAX];
    const char *path = pathname;
    size_t n;
    (void)dirfd;

    if (!buf || bufsiz == 0) {
        return -EINVAL;
    }

    if (!path) {
        path = "/proc/self/exe";
    } else {
        int ret = copy_user_cstring(pathname, kpath, sizeof(kpath));
        if (ret < 0) {
            return ret;
        }
        ret = kernel_user_cstring_ok(kpath, sizeof(kpath));
        if (ret < 0) {
            return ret;
        }
        path = kpath;
    }

    if (strcmp(path, "/proc/self/exe") == 0 ||
        strcmp(path, "self/exe") == 0 ||
        strcmp(path, "exe") == 0 ||
        strcmp(path, "/proc/thread-self/exe") == 0 ||
        path_ends_with(path, "/self/exe")) {
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
    if (vmm_copy_to_user(buf, target, n) < 0) {
        return -EFAULT;
    }
    return (int64_t)n;
}

static int64_t sys_faccessat(int dirfd, const char *pathname, int mode) {
    char resolved[PATH_MAX];
    struct dentry *dentry;
    int mask = 0;
    int ret = resolve_user_at_path_checked(dirfd, pathname, resolved, sizeof(resolved));
    if (ret < 0) {
        return ret;
    }
    if (mode & ~(R_OK | W_OK | X_OK | F_OK)) {
        return -EINVAL;
    }
    if (mode & R_OK) mask |= MAY_READ;
    if (mode & W_OK) mask |= MAY_WRITE;
    if (mode & X_OK) mask |= MAY_EXEC;

    dentry = vfs_lookup(resolved);
    if (!dentry) {
        return -ENOENT;
    }
    if (mask != 0) {
        struct inode *inode = dentry->d_inode;
        if (!inode) {
            dput(dentry);
            return -ENOENT;
        }
        if (inode->i_op && inode->i_op->permission) {
            ret = inode->i_op->permission(inode, mask);
        } else {
            ret = generic_permission(inode, mask);
        }
        if (ret < 0) {
            dput(dentry);
            return ret;
        }
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
    return sys_socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
}

static int64_t sys_nanosleep(const void *req, void *rem) {
    struct timespec_compat ts;
    uint64_t ms;
    if (!req) {
        return -EINVAL;
    }
    if (vmm_copy_from_user(&ts, req, sizeof(ts)) < 0) {
        return -EFAULT;
    }
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL) {
        return -EINVAL;
    }
    ms = (uint64_t)ts.tv_sec * 1000ULL;
    ms += (uint64_t)(ts.tv_nsec / 1000000LL);
    if (ms == 0 && ts.tv_nsec > 0) {
        ms = 1;
    }
    if (ms > 0) {
        scheduler_sleep_timeout(current, ms);
    }
    if (rem) {
        struct timespec_compat zero = {0, 0};
        if (vmm_copy_to_user(rem, &zero, sizeof(zero)) < 0) {
            return -EFAULT;
        }
    }
    return 0;
}

/* Linux x86_64 syscall #228: clock_gettime (minimal implementation) */
static int64_t sys_clock_gettime(uint64_t clockid, void *tp) {
    /* CLOCK_REALTIME=0, CLOCK_MONOTONIC=1 */
    if (!tp) {
        return -EFAULT;
    }
    /* Be permissive: treat unsupported clockids as "monotonic-ish". */
    (void)clockid;

    /* Use get_ticks() to avoid any dependence on a guessed CPU frequency. */
    uint64_t ms = get_ticks();
    struct timespec_compat ts = {
        .tv_sec = (int64_t)(ms / 1000ULL),
        .tv_nsec = (int64_t)((ms % 1000ULL) * 1000000ULL),
    };

    if (vmm_copy_to_user(tp, &ts, sizeof(ts)) < 0) {
        return -EFAULT;
    }
    return 0;
}

/* Linux x86_64 syscall #318: getrandom (best-effort pseudo-random) */
static int64_t sys_getrandom(void *buf, size_t buflen, uint64_t flags) {
    (void)flags;
    if (!buf && buflen != 0) {
        return -EFAULT;
    }
    if (buflen == 0) {
        return 0;
    }

    /* Seed from kernel time + pid. */
    uint64_t x = get_time_ns() ^ (uint64_t)(current ? current->pid : 0) ^ get_ticks();

    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;
    while (done < buflen) {
        /* xorshift64* */
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        x *= 0x2545F4914F6CDD1DULL;

        uint64_t word = x;
        size_t chunk = MIN(sizeof(word), buflen - done);
        if (vmm_copy_to_user(dst + done, &word, chunk) < 0) {
            return -EFAULT;
        }
        done += chunk;
    }

    return (int64_t)done;
}

static int poll_check_single_fd(int fd, int16_t events, int16_t *revents) {
    struct process *proc = current;
    struct file *file = NULL;
    struct ksocket *sock = socket_get_owned(fd);
    int16_t out = 0;

    if (revents) {
        *revents = 0;
    }
    if (fd < 0) {
        out = POLLERR;
        if (revents) *revents = out;
        return (out != 0);
    }

    if (sock) {
        if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
            if ((events & (POLLIN | POLLPRI)) && unix_socket_readable(sock) > 0) {
                out |= POLLIN;
            }
            if ((events & POLLOUT) && unix_socket_writable(sock) > 0) {
                out |= POLLOUT;
            }
            if (sock->peer_closed && sock->rx_len == 0) {
                out |= POLLHUP;
            }
        } else if (sock->type == SOCK_STREAM) {
            if ((events & POLLOUT) && sock->connected) {
                out |= POLLOUT;
            }
        } else {
            if (events & POLLOUT) {
                out |= POLLOUT;
            }
        }
        if (revents) *revents = out;
        return (out != 0);
    }

    if (IS_STDIO_FD(fd)) {
        if (events & (POLLIN | POLLPRI)) {
            out |= POLLIN;
        }
        if (events & POLLOUT) {
            out |= POLLOUT;
        }
        if (revents) *revents = out;
        return (out != 0);
    }

    if (!proc || !proc->files || fd >= (int)proc->files->max_fds) {
        out = POLLERR;
        if (revents) *revents = out;
        return 1;
    }
    file = fd_get(proc->files, fd);
    if (!file) {
        out = POLLERR;
        if (revents) *revents = out;
        return 1;
    }
    if (events & (POLLIN | POLLPRI)) out |= POLLIN;
    if (events & POLLOUT) out |= POLLOUT;
    if (revents) *revents = out;
    return (out != 0);
}

static int64_t sys_poll(void *fds, uint64_t nfds, int timeout) {
    struct pollfd_compat pf;
    uint64_t waited = 0;
    bool infinite = (timeout < 0);
    if (!fds && nfds > 0) {
        return -EFAULT;
    }
    if (nfds > 1024) {
        return -EINVAL;
    }
    for (;;) {
        int rc = maybe_handle_ctrlc();
        if (rc) {
            return rc;
        }
        int ready = 0;
        for (uint64_t i = 0; i < nfds; i++) {
            if (vmm_copy_from_user(&pf, (const void *)((uintptr_t)fds + (i * sizeof(pf))), sizeof(pf)) < 0) {
                return -EFAULT;
            }
            {
                int16_t revents_out = 0;

                if (poll_check_single_fd(pf.fd, pf.events, &revents_out)) {
                    ready++;
                }
                pf.revents = revents_out;
            }
            if (vmm_copy_to_user((void *)((uintptr_t)fds + (i * sizeof(pf))), &pf, sizeof(pf)) < 0) {
                return -EFAULT;
            }
        }
        if (ready > 0 || timeout == 0) {
            return ready;
        }
        if (!infinite && waited >= (uint64_t)timeout) {
            return 0;
        }
        scheduler_sleep_timeout(current, 1);
        waited++;
    }
}

#define FDSET_WORD_BITS 64
#define FDSET_MAX_WORDS 16

static bool fdset_test_bit(const uint64_t *bits, int fd) {
    return (bits[fd / FDSET_WORD_BITS] & (1ULL << (fd % FDSET_WORD_BITS))) != 0;
}

static void fdset_set_bit(uint64_t *bits, int fd) {
    bits[fd / FDSET_WORD_BITS] |= (1ULL << (fd % FDSET_WORD_BITS));
}

static int64_t sys_select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) {
    uint64_t in_r[FDSET_MAX_WORDS], in_w[FDSET_MAX_WORDS], in_e[FDSET_MAX_WORDS];
    uint64_t out_r[FDSET_MAX_WORDS], out_w[FDSET_MAX_WORDS], out_e[FDSET_MAX_WORDS];
    uint64_t waited = 0;
    uint64_t timeout_ms = 0;
    bool infinite = true;
    if (nfds < 0 || nfds > (FDSET_MAX_WORDS * FDSET_WORD_BITS)) {
        return -EINVAL;
    }
    if (timeout) {
        struct timeval_compat tv;
        if (vmm_copy_from_user(&tv, timeout, sizeof(tv)) < 0) {
            return -EFAULT;
        }
        if (tv.tv_sec < 0 || tv.tv_usec < 0) {
            return -EINVAL;
        }
        timeout_ms = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
        infinite = false;
    }
    memset(in_r, 0, sizeof(in_r));
    memset(in_w, 0, sizeof(in_w));
    memset(in_e, 0, sizeof(in_e));
    if (readfds && vmm_copy_from_user(in_r, readfds, sizeof(in_r)) < 0) return -EFAULT;
    if (writefds && vmm_copy_from_user(in_w, writefds, sizeof(in_w)) < 0) return -EFAULT;
    if (exceptfds && vmm_copy_from_user(in_e, exceptfds, sizeof(in_e)) < 0) return -EFAULT;

    for (;;) {
        int rc = maybe_handle_ctrlc();
        if (rc) {
            return rc;
        }
        int ready = 0;
        memset(out_r, 0, sizeof(out_r));
        memset(out_w, 0, sizeof(out_w));
        memset(out_e, 0, sizeof(out_e));
        for (int fd = 0; fd < nfds; fd++) {
            int16_t rev = 0;
            int16_t events = 0;
            if (fdset_test_bit(in_r, fd)) events |= (POLLIN | POLLPRI);
            if (fdset_test_bit(in_w, fd)) events |= POLLOUT;
            if (fdset_test_bit(in_e, fd)) events |= POLLERR;
            if (events == 0) continue;
            if (poll_check_single_fd(fd, events, &rev)) {
                if ((rev & (POLLIN | POLLPRI)) && fdset_test_bit(in_r, fd)) {
                    fdset_set_bit(out_r, fd);
                    ready++;
                }
                if ((rev & POLLOUT) && fdset_test_bit(in_w, fd)) {
                    fdset_set_bit(out_w, fd);
                    ready++;
                }
                if ((rev & (POLLERR | POLLHUP)) && fdset_test_bit(in_e, fd)) {
                    fdset_set_bit(out_e, fd);
                    ready++;
                }
            }
        }
        if (ready > 0 || (!infinite && timeout_ms == 0)) {
            if (readfds && vmm_copy_to_user(readfds, out_r, sizeof(out_r)) < 0) return -EFAULT;
            if (writefds && vmm_copy_to_user(writefds, out_w, sizeof(out_w)) < 0) return -EFAULT;
            if (exceptfds && vmm_copy_to_user(exceptfds, out_e, sizeof(out_e)) < 0) return -EFAULT;
            return ready;
        }
        if (!infinite && waited >= timeout_ms) {
            if (readfds && vmm_copy_to_user(readfds, out_r, sizeof(out_r)) < 0) return -EFAULT;
            if (writefds && vmm_copy_to_user(writefds, out_w, sizeof(out_w)) < 0) return -EFAULT;
            if (exceptfds && vmm_copy_to_user(exceptfds, out_e, sizeof(out_e)) < 0) return -EFAULT;
            return 0;
        }
        scheduler_sleep_timeout(current, 1);
        waited++;
    }
}

static int64_t sys_ioctl(int fd, unsigned long request, void *arg) {
    struct process *proc = current;
    struct file *file = NULL;

    if (proc && proc->files && fd >= 0 && !IS_STDIO_FD(fd)) {
        file = fd_get(proc->files, fd);
        if (!file) {
            return -EBADF;
        }
        if (file->f_op && file->f_op->ioctl) {
            int ret = file->f_op->ioctl(file, (unsigned int)request, (unsigned long)(uintptr_t)arg);
            put_file(file);
            return ret;
        }
        put_file(file);
    }

    switch (request) {
        case TCGETS: {
            if (!arg) return -EINVAL;
            struct termios_compat t;
            memset(&t, 0, sizeof(t));
            /* Canonical mode, echo, signal handling. */
            t.c_lflag = 0x00000001U | 0x00000002U | 0x00000008U; /* ISIG|ICANON|ECHO */
            t.c_cc[6] = 1;   /* VMIN */
            t.c_cc[5] = 0;   /* VTIME */
            if (vmm_copy_to_user(arg, &t, sizeof(t)) < 0) {
                return -EFAULT;
            }
            return 0;
        }
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
        case TIOCSCTTY:
        case TIOCNOTTY:
            return 0;
        case TIOCGPGRP: {
            int pgrp = current ? current->pid : 1;
            if (!arg) return -EINVAL;
            if (vmm_copy_to_user(arg, &pgrp, sizeof(pgrp)) < 0) {
                return -EFAULT;
            }
            return 0;
        }
        case TIOCSPGRP:
            return 0;
        case TIOCGWINSZ: {
            if (!arg) return -EINVAL;
            struct winsize ws;
            ws.ws_row = 25;
            ws.ws_col = 80;
            ws.ws_xpixel = 0;
            ws.ws_ypixel = 0;
            if (vmm_copy_to_user(arg, &ws, sizeof(ws)) < 0) {
                return -EFAULT;
            }
            return 0;
        }
        case KDGETMODE: {
            if (!arg) return -EINVAL;
            if (vmm_copy_to_user(arg, &tty_kd_mode, sizeof(tty_kd_mode)) < 0) {
                return -EFAULT;
            }
            return 0;
        }
        case KDSETMODE: {
            int mode = (int)(uintptr_t)arg;
            if (mode == KD_TEXT || mode == KD_GRAPHICS) {
                tty_kd_mode = mode;
                return 0;
            }
            return -EINVAL;
        }
        case KDGKBMODE: {
            if (!arg) return -EINVAL;
            if (vmm_copy_to_user(arg, &tty_kb_mode, sizeof(tty_kb_mode)) < 0) {
                return -EFAULT;
            }
            return 0;
        }
        case KDSKBMODE:
            tty_kb_mode = (int)(uintptr_t)arg;
            return 0;
        case VT_OPENQRY: {
            int vt = 1;
            if (!arg) return -EINVAL;
            if (vmm_copy_to_user(arg, &vt, sizeof(vt)) < 0) {
                return -EFAULT;
            }
            return 0;
        }
        case VT_GETSTATE: {
            struct vt_stat_compat st;
            if (!arg) return -EINVAL;
            st.v_active = 1;
            st.v_signal = 0;
            st.v_state = 1;
            if (vmm_copy_to_user(arg, &st, sizeof(st)) < 0) {
                return -EFAULT;
            }
            return 0;
        }
        case VT_GETMODE: {
            struct vt_mode_compat vm;
            if (!arg) return -EINVAL;
            memset(&vm, 0, sizeof(vm));
            if (vmm_copy_to_user(arg, &vm, sizeof(vm)) < 0) {
                return -EFAULT;
            }
            return 0;
        }
        case VT_SETMODE:
        case VT_ACTIVATE:
        case VT_WAITACTIVE:
            return 0;
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
    char *kbuf = NULL;
    size_t kcount = count;
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

    if (kcount > 64 * 1024) {
        kcount = 64 * 1024;
    }
    kbuf = kmalloc(kcount);
    if (!kbuf) {
        return -ENOMEM;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.buf = kbuf;
    ctx.count = kcount;
    ctx.written = 0;
    ctx.ctx.pos = file->f_pos;
    ctx.ctx.actor = filldir_getdents64;

    ret = file->f_op->readdir(file, &ctx.ctx);
    if (ret < 0) {
        kfree(kbuf);
        return ret;
    }
    file->f_pos = ctx.ctx.pos;
    if (ctx.written > 0) {
        if (vmm_copy_to_user(dirp, kbuf, ctx.written) < 0) {
            printk(KERN_ERR "getdents64: copy_to_user failed dirp=%p written=%lu count=%lu\n",
                   dirp, ctx.written, count);
            kfree(kbuf);
            return -EFAULT;
        }
    }
    kfree(kbuf);
    return (int64_t)ctx.written;
}

static int64_t sys_kill(pid_t pid, int sig) {
    return do_kill(pid, sig);
}

static int64_t sys_uname(void *buf) {
    struct utsname {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    } uname;

    memset(&uname, 0, sizeof(uname));
    strncpy(uname.sysname, "Obelisk", sizeof(uname.sysname) - 1);
    strncpy(uname.nodename, "obelisk", sizeof(uname.nodename) - 1);
    strncpy(uname.release, OBELISK_VERSION_STRING, sizeof(uname.release) - 1);
    strncpy(uname.version, "From Axioms, Order", sizeof(uname.version) - 1);
    strncpy(uname.machine, "x86_64", sizeof(uname.machine) - 1);
    strncpy(uname.domainname, "(none)", sizeof(uname.domainname) - 1);

    if (vmm_copy_to_user(buf, &uname, sizeof(uname)) < 0) {
        return -EFAULT;
    }
    return 0;
}

static int64_t sys_reboot(int magic1, int magic2, unsigned int cmd, void *arg) {
    (void)arg;
    /* Linux-compatible magic values so userland can call reboot sanely. */
    if ((uint32_t)magic1 != LINUX_REBOOT_MAGIC1 || (uint32_t)magic2 != LINUX_REBOOT_MAGIC2) {
        return -EINVAL;
    }

    printk(KERN_INFO "sys_reboot: cmd=0x%x (kernel shutdown path)\n", cmd);

    if (cmd == LINUX_REBOOT_CMD_RESTART || cmd == LINUX_REBOOT_CMD_CAD_ON) {
        kernel_reboot_hardware();
    }
    if (cmd == LINUX_REBOOT_CMD_POWER_OFF || cmd == LINUX_REBOOT_CMD_HALT) {
        kernel_poweroff_hardware();
    }

    return -EINVAL;
}

static int64_t sys_sysctl(void *args) {
    struct user_sysctl_args {
        const char *name;
        void *oldval;
        size_t *oldlenp;
        const void *newval;
        size_t newlen;
    };
    struct user_sysctl_args uargs;
    char kname[SYSCTL_PATH_MAX];
    int ret;

    if (!args) {
        return -EINVAL;
    }
    if (vmm_copy_from_user(&uargs, args, sizeof(uargs)) < 0) {
        return -EFAULT;
    }
    if (!uargs.name) {
        return -EINVAL;
    }
    ret = copy_user_cstring(uargs.name, kname, sizeof(kname));
    if (ret < 0) {
        return ret;
    }
    ret = kernel_user_cstring_ok(kname, sizeof(kname));
    if (ret < 0) {
        return ret;
    }

    /* Write path. */
    if (uargs.newval && uargs.newlen > 0) {
        void *kval;
        size_t newlen = uargs.newlen;
        if (newlen > 4096) {
            return -E2BIG;
        }
        kval = kmalloc(newlen);
        if (!kval) {
            return -ENOMEM;
        }
        if (vmm_copy_from_user(kval, uargs.newval, newlen) < 0) {
            kfree(kval);
            return -EFAULT;
        }
        ret = sysctl_write(kname, kval, newlen);
        kfree(kval);
        return ret;
    }

    /* Read path: return formatted string values for CLI compatibility. */
    if (!uargs.oldlenp) {
        return -EINVAL;
    }
    size_t out_len = 0;
    if (vmm_copy_from_user(&out_len, uargs.oldlenp, sizeof(out_len)) < 0) {
        return -EFAULT;
    }
    if (out_len == 0) {
        return -EINVAL;
    }
    if (!uargs.oldval) {
        return -EFAULT;
    }
    if (out_len > 1024) {
        out_len = 1024;
    }
    void *kout = kmalloc(out_len);
    if (!kout) {
        return -ENOMEM;
    }
    ret = sysctl_read_string(kname, (char *)kout, out_len);
    if (ret < 0) {
        kfree(kout);
        return ret;
    }
    out_len = strlen((const char *)kout) + 1;
    if (vmm_copy_to_user(uargs.oldval, kout, out_len) < 0) {
        kfree(kout);
        return -EFAULT;
    }
    if (vmm_copy_to_user(uargs.oldlenp, &out_len, sizeof(out_len)) < 0) {
        kfree(kout);
        return -EFAULT;
    }
    kfree(kout);
    return 0;
}

static int64_t sys_socket(int domain, int type, int protocol) {
    int fd;
    if (type != SOCK_RAW && type != SOCK_DGRAM && type != SOCK_STREAM) {
        return -EPROTONOSUPPORT;
    }
    if (domain == AF_UNIX) {
        if (type != SOCK_STREAM) {
            return -EPROTONOSUPPORT;
        }
        if (protocol != 0) {
            return -EPROTONOSUPPORT;
        }
        fd = socket_alloc_slot(current ? current->pid : 0);
        if (fd < 0) {
            return fd;
        }
        struct ksocket *sock = socket_get_owned(fd);
        sock->domain = AF_UNIX;
        sock->type = SOCK_STREAM;
        sock->protocol = 0;
        return fd;
    }
    if (domain != AF_INET) {
        return -EAFNOSUPPORT;
    }
    if (type == SOCK_RAW) {
        if (protocol != 0 && protocol != IPPROTO_ICMP) {
            return -EPROTONOSUPPORT;
        }
    } else if (type == SOCK_DGRAM) {
        if (protocol != 0 && protocol != IPPROTO_UDP) {
            return -EPROTONOSUPPORT;
        }
    } else if (type == SOCK_STREAM) {
        if (protocol != 0 && protocol != IPPROTO_TCP) {
            return -EPROTONOSUPPORT;
        }
    }
    if (!net_is_ready()) {
        return -ENETDOWN;
    }
    fd = socket_alloc_slot(current ? current->pid : 0);
    if (fd < 0) {
        return fd;
    }
    struct ksocket *sock = socket_get_owned(fd);
    sock->domain = domain;
    sock->type = type;
    sock->protocol = (protocol == 0) ?
        ((type == SOCK_RAW) ? IPPROTO_ICMP :
         ((type == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP)) : protocol;
    return fd;
}

static int64_t sys_connect(int sockfd, const void *addr, int addrlen) {
    struct ksocket *sock = socket_get_owned(sockfd);
    struct sockaddr_in_compat sa;
    uint8_t dst_ip[4];
    uint16_t dst_port;
    uint16_t src_port;
    int conn_id = -1;
    int idx;
    int loops = 30000;
    int st;
    if (!sock) {
        return -EBADF;
    }
    if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
        struct sockaddr_un_compat sun;
        char path[UNIX_PATH_MAX];
        if (!addr || addrlen < (int)sizeof(uint16_t)) {
            return -EINVAL;
        }
        memset(&sun, 0, sizeof(sun));
        if (vmm_copy_from_user(&sun, addr, MIN((size_t)addrlen, sizeof(sun))) < 0) {
            return -EFAULT;
        }
        if (sun.sun_family != AF_UNIX) {
            return -EAFNOSUPPORT;
        }
        strncpy(path, sun.sun_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        if (path[0] == '\0') {
            return -EINVAL;
        }
        struct ksocket *listener = NULL;
        for (int i = 0; i < SOCKET_MAX; i++) {
            struct ksocket *cand = &socket_table[i];
            if (!cand->used || cand->domain != AF_UNIX || !cand->listening || !cand->bound) {
                continue;
            }
            if (strncmp(cand->unix_path, path, sizeof(cand->unix_path)) == 0) {
                listener = cand;
                break;
            }
        }
        if (!listener) {
            return -ECONNREFUSED;
        }
        int accepted_fd = socket_alloc_slot(listener->owner_pid);
        if (accepted_fd < 0) {
            return accepted_fd;
        }
        struct ksocket *accepted = socket_get_any(accepted_fd);
        accepted->domain = AF_UNIX;
        accepted->type = SOCK_STREAM;
        accepted->protocol = 0;
        accepted->connected = true;
        accepted->peer_fd = sockfd;

        sock->connected = true;
        sock->peer_closed = false;
        sock->peer_fd = accepted_fd;

        if (unix_socket_queue_pending(listener, accepted_fd) < 0) {
            memset(accepted, 0, sizeof(*accepted));
            sock->connected = false;
            sock->peer_fd = -1;
            return -EAGAIN;
        }
        return 0;
    }
    if (sock->domain != AF_INET) {
        return -EAFNOSUPPORT;
    }
    if (sock->type != SOCK_STREAM || sock->protocol != IPPROTO_TCP) {
        return -EOPNOTSUPP;
    }
    if (!addr || addrlen < (int)sizeof(sa)) {
        return -EINVAL;
    }
    if (vmm_copy_from_user(&sa, addr, sizeof(sa)) < 0) {
        return -EFAULT;
    }
    if (sa.sin_family != AF_INET) {
        return -EAFNOSUPPORT;
    }
    dst_port = ntohs16(sa.sin_port);
    if (dst_port == 0) {
        return -EINVAL;
    }
    dst_ip[0] = (uint8_t)((sa.sin_addr >> 24) & 0xFF);
    dst_ip[1] = (uint8_t)((sa.sin_addr >> 16) & 0xFF);
    dst_ip[2] = (uint8_t)((sa.sin_addr >> 8) & 0xFF);
    dst_ip[3] = (uint8_t)(sa.sin_addr & 0xFF);

    if (sock->bound_port == 0) {
        idx = socket_fd_to_index(sockfd);
        src_port = (uint16_t)(45000 + ((idx >= 0) ? (uint16_t)idx : 0));
        sock->bound_port = src_port;
    } else {
        src_port = sock->bound_port;
    }

    st = net_tcp_connect(dst_ip, dst_port, src_port, &conn_id);
    if (st < 0) {
        return st;
    }

    while (loops-- > 0) {
        st = net_tcp_is_connected(conn_id);
        if (st == 1) {
            sock->tcp_conn_id = conn_id;
            sock->connected = true;
            memcpy(sock->remote_ip, dst_ip, 4);
            sock->remote_port = dst_port;
            return 0;
        }
        if (st < 0) {
            (void)net_tcp_close(conn_id);
            return st;
        }
        net_tick();
        scheduler_yield();
    }

    (void)net_tcp_close(conn_id);
    return -ETIMEDOUT;
}

static int64_t sys_accept(int sockfd, void *addr, int *addrlen) {
    struct ksocket *sock = socket_get_owned(sockfd);
    if (!sock) {
        return -EBADF;
    }
    if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
        int child_fd = unix_socket_pop_pending(sock);
        if (child_fd < 0) {
            return child_fd;
        }
        if (addr && addrlen) {
            int user_len = 0;
            struct sockaddr_un_compat sun;
            if (vmm_copy_from_user(&user_len, addrlen, sizeof(user_len)) == 0) {
                if (user_len >= (int)sizeof(sun)) {
                    memset(&sun, 0, sizeof(sun));
                    sun.sun_family = AF_UNIX;
                    if (vmm_copy_to_user(addr, &sun, sizeof(sun)) < 0) {
                        return -EFAULT;
                    }
                    user_len = sizeof(sun);
                    if (vmm_copy_to_user(addrlen, &user_len, sizeof(user_len)) < 0) {
                        return -EFAULT;
                    }
                }
            }
        }
        return child_fd;
    }
    return -EOPNOTSUPP;
}

static int64_t sys_sendto(int sockfd, const void *buf, size_t len, int flags, const void *dest_addr, int addrlen) {
    struct ksocket *sock = socket_get_owned(sockfd);
    struct sockaddr_in_compat sa;
    uint8_t payload[2048];
    uint8_t dst_ip[4];
    uint16_t dst_port;
    uint16_t src_port;
    int ret;
    (void)flags;
    if (!sock) {
        return -EBADF;
    }
    if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
        uint8_t upayload[1024];
        struct ksocket *peer;
        int pushed;
        (void)dest_addr;
        (void)addrlen;
        if (!sock->connected || sock->peer_fd < 0) {
            return -ENOTCONN;
        }
        if (!buf || len == 0) {
            return 0;
        }
        if (len > sizeof(upayload)) {
            len = sizeof(upayload);
        }
        if (vmm_copy_from_user(upayload, buf, len) < 0) {
            return -EFAULT;
        }
        peer = socket_get_any(sock->peer_fd);
        if (!peer || !peer->used) {
            return -EPIPE;
        }
        pushed = unix_socket_rx_push(peer, upayload, len);
        if (pushed < 0) {
            return pushed;
        }
        return pushed;
    }
    if (sock->domain != AF_INET) {
        return -EAFNOSUPPORT;
    }
    if (len == 0) {
        return 0;
    }
    if (!buf || len > sizeof(payload)) {
        return -EMSGSIZE;
    }
    if (vmm_copy_from_user(payload, buf, len) < 0) {
        return -EFAULT;
    }

    if (sock->type == SOCK_RAW && sock->protocol == IPPROTO_ICMP) {
        if (!dest_addr || addrlen < (int)sizeof(sa)) {
            return -EINVAL;
        }
        if (vmm_copy_from_user(&sa, dest_addr, sizeof(sa)) < 0) {
            return -EFAULT;
        }
        if (sa.sin_family != AF_INET) {
            return -EAFNOSUPPORT;
        }
        dst_ip[0] = (uint8_t)((sa.sin_addr >> 24) & 0xFF);
        dst_ip[1] = (uint8_t)((sa.sin_addr >> 16) & 0xFF);
        dst_ip[2] = (uint8_t)((sa.sin_addr >> 8) & 0xFF);
        dst_ip[3] = (uint8_t)(sa.sin_addr & 0xFF);
        int retries = 200;
        unsigned ctrlc_check = 0;
        do {
            if (((ctrlc_check++) & 7U) == 0U) {
                int rc = maybe_handle_ctrlc();
                if (rc) {
                    return rc;
                }
            }
            ret = net_send_icmp_echo(dst_ip, payload, len);
            if (ret != -EAGAIN) {
                break;
            }
            net_tick();
            scheduler_yield();
        } while (--retries > 0);
    } else if (sock->type == SOCK_DGRAM && sock->protocol == IPPROTO_UDP) {
        if (!dest_addr || addrlen < (int)sizeof(sa)) {
            return -EINVAL;
        }
        if (vmm_copy_from_user(&sa, dest_addr, sizeof(sa)) < 0) {
            return -EFAULT;
        }
        if (sa.sin_family != AF_INET) {
            return -EAFNOSUPPORT;
        }
        dst_ip[0] = (uint8_t)((sa.sin_addr >> 24) & 0xFF);
        dst_ip[1] = (uint8_t)((sa.sin_addr >> 16) & 0xFF);
        dst_ip[2] = (uint8_t)((sa.sin_addr >> 8) & 0xFF);
        dst_ip[3] = (uint8_t)(sa.sin_addr & 0xFF);
        dst_port = ntohs16(sa.sin_port);
        if (dst_port == 0) {
            return -EINVAL;
        }
        if (sock->bound_port == 0) {
            int idx = socket_fd_to_index(sockfd);
            src_port = (uint16_t)(40000 + ((idx >= 0) ? (uint16_t)idx : 0));
            sock->bound_port = src_port;
        } else {
            src_port = sock->bound_port;
        }
        {
            int retries = 200;
            unsigned ctrlc_check = 0;
            do {
                if (((ctrlc_check++) & 7U) == 0U) {
                    int rc = maybe_handle_ctrlc();
                    if (rc) {
                        return rc;
                    }
                }
                ret = net_send_udp(dst_ip, src_port, dst_port, payload, len);
                if (ret != -EAGAIN) {
                    break;
                }
                net_tick();
                scheduler_yield();
            } while (--retries > 0);
        }
    } else if (sock->type == SOCK_STREAM && sock->protocol == IPPROTO_TCP) {
        int retries = 200;
        if (!sock->connected || sock->tcp_conn_id < 0) {
            return -ENOTCONN;
        }
        unsigned ctrlc_check = 0;
        do {
            if (((ctrlc_check++) & 7U) == 0U) {
                int rc = maybe_handle_ctrlc();
                if (rc) {
                    return rc;
                }
            }
            ret = net_tcp_send(sock->tcp_conn_id, payload, len);
            if (ret != -EAGAIN) {
                break;
            }
            net_tick();
            scheduler_yield();
        } while (--retries > 0);
    } else {
        return -EAFNOSUPPORT;
    }
    if (ret < 0) {
        return ret;
    }
    return (int64_t)len;
}

static int64_t sys_recvfrom(int sockfd, void *buf, size_t len, int flags, void *src_addr, int *addrlen) {
    struct ksocket *sock = socket_get_owned(sockfd);
    struct net_icmp_event ev;
    struct net_udp_event uev;
    size_t out_len;
    int ret;
    int max_loops = 20000; /* Poll-bound receive timeout window. */
    bool peer_closed = false;
    (void)flags;
    if (!sock) {
        return -EBADF;
    }
    if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
        uint8_t kbuf[1024];
        int got;
        (void)flags;
        (void)src_addr;
        (void)addrlen;
        if (len == 0) {
            return 0;
        }
        if (!buf) {
            return -EFAULT;
        }
        if (len > sizeof(kbuf)) {
            len = sizeof(kbuf);
        }
        got = unix_socket_rx_pop(sock, kbuf, len);
        if (got < 0) {
            return got;
        }
        if (got == 0) {
            if (sock->peer_closed) {
                return 0;
            }
            return -EAGAIN;
        }
        if (vmm_copy_to_user(buf, kbuf, (size_t)got) < 0) {
            return -EFAULT;
        }
        return got;
    }
    if (sock->domain != AF_INET) {
        return -EAFNOSUPPORT;
    }
    if (len == 0) {
        return 0;
    }
    if (!buf) {
        return -EFAULT;
    }

    if (sock->type == SOCK_RAW && sock->protocol == IPPROTO_ICMP) {
        unsigned ctrlc_check = 0;
        while (max_loops-- > 0) {
            if (((ctrlc_check++) & 7U) == 0U) {
                int rc = maybe_handle_ctrlc();
                if (rc) {
                    return rc;
                }
            }
            ret = net_recv_icmp_echo_event(&ev);
            if (ret == 0) {
                break;
            }
            if (ret != -EAGAIN) {
                return ret;
            }
            net_tick();
            scheduler_yield();
        }
        if (ret != 0) {
            return -EAGAIN;
        }

        out_len = ev.packet_len;
        if (out_len > len) {
            out_len = len;
        }
        if (vmm_copy_to_user(buf, ev.packet, out_len) < 0) {
            return -EFAULT;
        }

        if (src_addr && addrlen) {
            struct sockaddr_in_compat sa;
            int user_len;
            if (vmm_copy_from_user(&user_len, addrlen, sizeof(user_len)) < 0) {
                return -EFAULT;
            }
            if (user_len >= (int)sizeof(sa)) {
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_addr = ((uint32_t)ev.src_ip[0] << 24) |
                              ((uint32_t)ev.src_ip[1] << 16) |
                              ((uint32_t)ev.src_ip[2] << 8) |
                              (uint32_t)ev.src_ip[3];
                if (vmm_copy_to_user(src_addr, &sa, sizeof(sa)) < 0) {
                    return -EFAULT;
                }
                user_len = sizeof(sa);
                if (vmm_copy_to_user(addrlen, &user_len, sizeof(user_len)) < 0) {
                    return -EFAULT;
                }
            }
        }
        return (int64_t)out_len;
    } else if (sock->type == SOCK_DGRAM && sock->protocol == IPPROTO_UDP) {
        unsigned ctrlc_check = 0;
        while (max_loops-- > 0) {
            if (((ctrlc_check++) & 7U) == 0U) {
                int rc = maybe_handle_ctrlc();
                if (rc) {
                    return rc;
                }
            }
            ret = net_recv_udp_event(sock->bound_port, &uev);
            if (ret == 0) {
                break;
            }
            if (ret != -EAGAIN) {
                return ret;
            }
            net_tick();
            scheduler_yield();
        }
        if (ret != 0) {
            return -EAGAIN;
        }
        out_len = uev.payload_len;
        if (out_len > len) {
            out_len = len;
        }
        if (vmm_copy_to_user(buf, uev.payload, out_len) < 0) {
            return -EFAULT;
        }
        if (src_addr && addrlen) {
            struct sockaddr_in_compat sa;
            int user_len;
            if (vmm_copy_from_user(&user_len, addrlen, sizeof(user_len)) < 0) {
                return -EFAULT;
            }
            if (user_len >= (int)sizeof(sa)) {
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_port = htons16(uev.src_port);
                sa.sin_addr = ((uint32_t)uev.src_ip[0] << 24) |
                              ((uint32_t)uev.src_ip[1] << 16) |
                              ((uint32_t)uev.src_ip[2] << 8) |
                              (uint32_t)uev.src_ip[3];
                if (vmm_copy_to_user(src_addr, &sa, sizeof(sa)) < 0) {
                    return -EFAULT;
                }
                user_len = sizeof(sa);
                if (vmm_copy_to_user(addrlen, &user_len, sizeof(user_len)) < 0) {
                    return -EFAULT;
                }
            }
        }
        return (int64_t)out_len;
    } else if (sock->type == SOCK_STREAM && sock->protocol == IPPROTO_TCP) {
        uint8_t tcp_buf[2048];
        size_t want = len;
        if (want > sizeof(tcp_buf)) {
            want = sizeof(tcp_buf);
        }
        if (!sock->connected || sock->tcp_conn_id < 0) {
            return -ENOTCONN;
        }
        unsigned ctrlc_check = 0;
        while (max_loops-- > 0) {
            if (((ctrlc_check++) & 7U) == 0U) {
                int rc = maybe_handle_ctrlc();
                if (rc) {
                    return rc;
                }
            }
            ret = net_tcp_recv(sock->tcp_conn_id, tcp_buf, want, &peer_closed);
            if (ret >= 0) {
                break;
            }
            if (ret != -EAGAIN) {
                return ret;
            }
            net_tick();
            scheduler_yield();
        }
        if (ret < 0) {
            return -EAGAIN;
        }
        if (ret > 0) {
            if (vmm_copy_to_user(buf, tcp_buf, (size_t)ret) < 0) {
                return -EFAULT;
            }
        }
        if (src_addr && addrlen) {
            struct sockaddr_in_compat sa;
            int user_len;
            if (vmm_copy_from_user(&user_len, addrlen, sizeof(user_len)) < 0) {
                return -EFAULT;
            }
            if (user_len >= (int)sizeof(sa)) {
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_port = htons16(sock->remote_port);
                sa.sin_addr = ((uint32_t)sock->remote_ip[0] << 24) |
                              ((uint32_t)sock->remote_ip[1] << 16) |
                              ((uint32_t)sock->remote_ip[2] << 8) |
                              (uint32_t)sock->remote_ip[3];
                if (vmm_copy_to_user(src_addr, &sa, sizeof(sa)) < 0) {
                    return -EFAULT;
                }
                user_len = sizeof(sa);
                if (vmm_copy_to_user(addrlen, &user_len, sizeof(user_len)) < 0) {
                    return -EFAULT;
                }
            }
        }
        if (ret == 0 && peer_closed) {
            return 0;
        }
        return (int64_t)ret;
    }
    return -EAFNOSUPPORT;
}

static int64_t sys_bind(int sockfd, const void *addr, int addrlen) {
    struct ksocket *sock = socket_get_owned(sockfd);
    struct sockaddr_in_compat sa;
    if (!sock) {
        return -EBADF;
    }
    if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
        struct sockaddr_un_compat sun;
        if (!addr || addrlen < (int)sizeof(uint16_t)) {
            return -EINVAL;
        }
        memset(&sun, 0, sizeof(sun));
        if (vmm_copy_from_user(&sun, addr, MIN((size_t)addrlen, sizeof(sun))) < 0) {
            return -EFAULT;
        }
        if (sun.sun_family != AF_UNIX) {
            return -EAFNOSUPPORT;
        }
        if (sun.sun_path[0] == '\0') {
            return -EINVAL;
        }
        for (int i = 0; i < SOCKET_MAX; i++) {
            if (!socket_table[i].used || socket_table[i].domain != AF_UNIX || !socket_table[i].bound) {
                continue;
            }
            if (strncmp(socket_table[i].unix_path, sun.sun_path, sizeof(socket_table[i].unix_path)) == 0) {
                return -EADDRINUSE;
            }
        }
        strncpy(sock->unix_path, sun.sun_path, sizeof(sock->unix_path) - 1);
        sock->unix_path[sizeof(sock->unix_path) - 1] = '\0';
        sock->bound = true;
        return 0;
    }
    if (sock->domain != AF_INET) {
        return -EAFNOSUPPORT;
    }
    if (!addr || addrlen < (int)sizeof(sa)) {
        return -EINVAL;
    }
    if (vmm_copy_from_user(&sa, addr, sizeof(sa)) < 0) {
        return -EFAULT;
    }
    if (sa.sin_family != AF_INET) {
        return -EAFNOSUPPORT;
    }
    sock->bound_port = ntohs16(sa.sin_port);
    return 0;
}

static int64_t sys_listen(int sockfd, int backlog) {
    struct ksocket *sock = socket_get_owned(sockfd);
    (void)backlog;
    if (!sock) {
        return -EBADF;
    }
    if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
        if (!sock->bound) {
            return -EINVAL;
        }
        sock->listening = true;
        return 0;
    }
    return -EOPNOTSUPP;
}

static int64_t sys_shutdown(int sockfd, int how) {
    struct ksocket *sock = socket_get_owned(sockfd);
    (void)how;
    if (!sock) {
        return -EBADF;
    }
    if (sock->domain == AF_UNIX && sock->type == SOCK_STREAM) {
        if (sock->peer_fd >= 0) {
            struct ksocket *peer = socket_get_any(sock->peer_fd);
            if (peer && peer->used) {
                peer->peer_closed = true;
                if (peer->peer_fd == sockfd) {
                    peer->peer_fd = -1;
                    peer->connected = false;
                }
            }
        }
        sock->peer_closed = true;
        sock->connected = false;
        sock->peer_fd = -1;
        return 0;
    }
    if (sock->type == SOCK_STREAM && sock->protocol == IPPROTO_TCP && sock->tcp_conn_id >= 0) {
        (void)net_tcp_close(sock->tcp_conn_id);
        sock->tcp_conn_id = -1;
        sock->connected = false;
    }
    return 0;
}

static int64_t sys_socketpair(int domain, int type, int protocol, int *sv) {
    int fd0, fd1;
    struct ksocket *a, *b;
    int pair[2];
    if (!sv) {
        return -EFAULT;
    }
    if (domain != AF_UNIX || type != SOCK_STREAM || protocol != 0) {
        return -EOPNOTSUPP;
    }
    fd0 = socket_alloc_slot(current ? current->pid : 0);
    if (fd0 < 0) {
        return fd0;
    }
    fd1 = socket_alloc_slot(current ? current->pid : 0);
    if (fd1 < 0) {
        struct ksocket *sa = socket_get_any(fd0);
        if (sa) memset(sa, 0, sizeof(*sa));
        return fd1;
    }
    a = socket_get_any(fd0);
    b = socket_get_any(fd1);
    if (!a || !b) {
        return -EBADF;
    }
    a->domain = AF_UNIX;
    a->type = SOCK_STREAM;
    a->protocol = 0;
    a->connected = true;
    a->peer_fd = fd1;

    b->domain = AF_UNIX;
    b->type = SOCK_STREAM;
    b->protocol = 0;
    b->connected = true;
    b->peer_fd = fd0;

    pair[0] = fd0;
    pair[1] = fd1;
    if (vmm_copy_to_user(sv, pair, sizeof(pair)) < 0) {
        memset(a, 0, sizeof(*a));
        memset(b, 0, sizeof(*b));
        return -EFAULT;
    }
    return 0;
}

/* Linux x86_64 syscall #157: prctl (minimal subset) */
static int64_t sys_prctl(uint64_t code, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;

    /* Linux constants (x86_64). */
    const uint64_t PR_SET_NAME = 15;
    const uint64_t PR_GET_NAME = 16;

    struct process *p = current;
    if (!p) {
        return -ESRCH;
    }

    if (code == PR_SET_NAME) {
        /* Keep process diagnostic comm stable (startup correctness doesn't depend on PR_SET_NAME). */
        if (!arg2) {
            return -EFAULT;
        }
        /* Minimal validation: make sure the user pointer is readable. */
        char probe = 0;
        if (vmm_copy_from_user(&probe, (const void *)arg2, 1) < 0) {
            return -EFAULT;
        }
        return 0;
    }

    if (code == PR_GET_NAME) {
        if (!arg2) {
            return -EFAULT;
        }
        /* Provide NUL-terminated comm in a fixed-size buffer. */
        char name[16];
        memset(name, 0, sizeof(name));
        strncpy(name, p->comm, sizeof(name) - 1);
        if (vmm_copy_to_user((void *)arg2, name, sizeof(name)) < 0) {
            return -EFAULT;
        }
        return 0;
    }

    /* Keep behavior conservative: unsupported prctl codes behave like ENOSYS. */
    return -ENOSYS;
}

static int64_t sys_arch_prctl(int code, uint64_t addr) {
    struct process *p = current;
    if (!p) {
        return -ESRCH;
    }

    switch (code) {
        case ARCH_SET_FS:
            p->fs_base = addr;
            wrmsr(MSR_FS_BASE, p->fs_base);
            return 0;
        case ARCH_SET_GS:
            p->gs_base = addr;
            wrmsr(MSR_GS_BASE, p->gs_base);
            return 0;
        case ARCH_GET_FS:
            if (vmm_copy_to_user((void *)(uintptr_t)addr, &p->fs_base, sizeof(p->fs_base)) < 0) {
                return -EFAULT;
            }
            return 0;
        case ARCH_GET_GS:
            if (vmm_copy_to_user((void *)(uintptr_t)addr, &p->gs_base, sizeof(p->gs_base)) < 0) {
                return -EFAULT;
            }
            return 0;
        default:
            return -EINVAL;
    }
}

/* ==========================================================================
 * Syscall initialization
 * ========================================================================== */

/* Set up SYSCALL/SYSRET MSRs */
static void syscall_msr_init(void) {
    /* Keep fast SYSCALL disabled for now.
     * Our userspace runs reliably via INT 0x80, while the fast path still
     * needs a dedicated kernel-stack handoff on entry. */
    uint64_t efer = rdmsr(MSR_EFER);
    efer &= ~MSR_EFER_SCE;
    wrmsr(MSR_EFER, efer);
}

/* Initialize syscall subsystem */
void syscall_init(void) {
    printk(KERN_INFO "Initializing syscall interface...\n");
    
    syscall_table_init();
    syscall_msr_init();
    printk(KERN_WARNING "syscall: fast SYSCALL/SYSRET path disabled, using INT 0x80 ABI\n");
    
    printk(KERN_INFO "Syscall interface initialized (%d syscalls registered)\n",
           NR_SYSCALLS);
}