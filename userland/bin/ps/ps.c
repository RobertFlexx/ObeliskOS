/*
 * Obelisk — ps(1): process list via obelisk_proc_list syscall (kernel sysctl system.proc.list).
 */

#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int obelisk_proc_list(void *buf, size_t *lenp);

int main(int argc, char **argv) {
    char buf[8192];
    size_t n = sizeof(buf);
    int r;

    (void)argv;
    if (argc > 1) {
        (void)write(2, "usage: ps\n", 10);
        return 1;
    }
    r = obelisk_proc_list(buf, &n);
    if (r < 0) {
        (void)write(2, "ps: cannot read process list\n", 29);
        return 1;
    }
    (void)write(1, "  PID COMMAND\n", 14);
    (void)write(1, buf, n);
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
