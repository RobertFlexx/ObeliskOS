/*
 * Obelisk OS - System Call Wrappers
 * From Axioms, Order.
 */

#include <stdint.h>
#include <stddef.h>

typedef long ssize_t;
typedef long off_t;

static inline long ob_syscall0(long n) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall1(long n, long a1) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall4(long n, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "cc", "memory"
    );
    return ret;
}

static inline long ob_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "cc", "memory"
    );
    return ret;
}

/* Read from file descriptor */
ssize_t read(int fd, void *buf, size_t count) {
    return ob_syscall3(0, fd, (long)buf, count);
}

/* Write to file descriptor */
ssize_t write(int fd, const void *buf, size_t count) {
    return ob_syscall3(1, fd, (long)buf, count);
}

/* Open file */
int open(const char *pathname, int flags, int mode) {
    return ob_syscall3(2, (long)pathname, flags, mode);
}

/* Close file descriptor */
int close(int fd) {
    return ob_syscall1(3, fd);
}

/* Get file status */
int stat(const char *pathname, void *statbuf) {
    return ob_syscall2(4, (long)pathname, (long)statbuf);
}

/* Seek in file */
off_t lseek(int fd, off_t offset, int whence) {
    return ob_syscall3(8, fd, offset, whence);
}

/* Memory map */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return (void *)ob_syscall6(9, (long)addr, length, prot, flags, fd, offset);
}

/* Memory unmap */
int munmap(void *addr, size_t length) {
    return ob_syscall2(11, (long)addr, length);
}

/* Program break */
void *brk(void *addr) {
    return (void *)ob_syscall1(12, (long)addr);
}

/* Fork process */
int fork(void) {
    return ob_syscall0(57);
}

/* Execute program */
int execve(const char *pathname, char *const argv[], char *const envp[]) {
    return ob_syscall3(59, (long)pathname, (long)argv, (long)envp);
}

/* Exit process */
void _exit(int status) {
    ob_syscall1(60, status);
    __builtin_unreachable();
}

/* Wait for child */
int waitpid(int pid, int *status, int options) {
    return ob_syscall4(61, pid, (long)status, options, 0);
}

/* Get process ID */
int getpid(void) {
    return ob_syscall0(39);
}

/* Get parent process ID */
int getppid(void) {
    return ob_syscall0(110);
}

/* Get user ID */
int getuid(void) {
    return ob_syscall0(102);
}

/* Get group ID */
int getgid(void) {
    return ob_syscall0(104);
}

/* Get effective user ID */
int geteuid(void) {
    return ob_syscall0(107);
}

/* Get effective group ID */
int getegid(void) {
    return ob_syscall0(108);
}

/* Set user ID */
int setuid(int uid) {
    return ob_syscall1(105, uid);
}

/* Set group ID */
int setgid(int gid) {
    return ob_syscall1(106, gid);
}

/* Change directory */
int chdir(const char *path) {
    return ob_syscall1(80, (long)path);
}

/* Get current directory */
char *getcwd(char *buf, size_t size) {
    if (ob_syscall2(79, (long)buf, size) < 0) {
        return NULL;
    }
    return buf;
}

/* Create directory */
int mkdir(const char *pathname, int mode) {
    return ob_syscall2(83, (long)pathname, mode);
}

/* Remove directory */
int rmdir(const char *pathname) {
    return ob_syscall1(84, (long)pathname);
}

/* Remove file */
int unlink(const char *pathname) {
    return ob_syscall1(87, (long)pathname);
}

/* Rename file */
int rename(const char *oldpath, const char *newpath) {
    return ob_syscall2(82, (long)oldpath, (long)newpath);
}

/* Duplicate file descriptor */
int dup(int oldfd) {
    return ob_syscall1(32, oldfd);
}

/* Duplicate file descriptor to specific number */
int dup2(int oldfd, int newfd) {
    return ob_syscall2(33, oldfd, newfd);
}

/* Create pipe */
int pipe(int pipefd[2]) {
    return ob_syscall1(22, (long)pipefd);
}

/* Create socket */
int socket(int domain, int type, int protocol) {
    return ob_syscall3(41, domain, type, protocol);
}

/* Connect socket */
int connect(int sockfd, const void *addr, int addrlen) {
    return ob_syscall3(42, sockfd, (long)addr, addrlen);
}

/* Accept connection */
int accept(int sockfd, void *addr, int *addrlen) {
    return ob_syscall3(43, sockfd, (long)addr, (long)addrlen);
}

/* Send data */
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const void *dest_addr, int addrlen) {
    return ob_syscall6(44, sockfd, (long)buf, len, flags, (long)dest_addr, addrlen);
}

/* Receive data */
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, void *src_addr, int *addrlen) {
    return ob_syscall6(45, sockfd, (long)buf, len, flags, (long)src_addr, (long)addrlen);
}

/* Bind socket */
int bind(int sockfd, const void *addr, int addrlen) {
    return ob_syscall3(49, sockfd, (long)addr, addrlen);
}

/* Listen on socket */
int listen(int sockfd, int backlog) {
    return ob_syscall2(50, sockfd, backlog);
}

/* Shutdown socket */
int shutdown(int sockfd, int how) {
    return ob_syscall2(48, sockfd, how);
}

/* Send signal */
int kill(int pid, int sig) {
    return ob_syscall2(62, pid, sig);
}

/* sysctl */
int sysctl(void *args) {
    return ob_syscall1(400, (long)args);
}

/*
 * Minimal assert handler for -betterC D objects.
 * DMD may emit calls to __assert for safety checks in low-level code.
 */
void __assert(void) {
    const char msg[] = "fatal: __assert\n";
    write(2, msg, sizeof(msg) - 1);
    _exit(127);
}