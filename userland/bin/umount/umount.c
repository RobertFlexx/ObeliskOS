/*
 * Obelisk — umount(8): unmount target path (umount2 syscall).
 */

#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int umount2(const char *target, int flags);

static void wrerr(const char *s) {
    (void)write(2, s, strlen(s));
}

int main(int argc, char **argv) {
    int flags = 0;

    if (argc < 2) {
        wrerr("usage: umount target\n");
        return 1;
    }
    if (argc > 2) {
        wrerr("usage: umount target\n");
        return 1;
    }
    if (umount2(argv[1], flags) < 0) {
        wrerr("umount: failed\n");
        return 1;
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
