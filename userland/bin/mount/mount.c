/*
 * Obelisk — mount(8): mount -t fstype source target
 */

#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data);

static void wrerr(const char *s) {
    (void)write(2, s, strlen(s));
}

int main(int argc, char **argv) {
    const char *fstype = NULL;
    const char *source = NULL;
    const char *target = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                wrerr("mount: -t needs argument\n");
                return 1;
            }
            fstype = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            wrerr("mount: unknown option\n");
            return 1;
        }
        if (!source) {
            source = argv[i];
        } else if (!target) {
            target = argv[i];
        } else {
            wrerr("mount: too many arguments\n");
            return 1;
        }
    }
    if (!fstype || !source || !target) {
        wrerr("usage: mount -t fstype source target\n");
        return 1;
    }
    if (mount(source, target, fstype, 0, NULL) < 0) {
        wrerr("mount: failed\n");
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
