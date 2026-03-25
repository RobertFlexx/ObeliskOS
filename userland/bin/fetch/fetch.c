/*
 * Obelisk OS - fetch utility (minimal wrapper)
 * From Axioms, Order.
 *
 * This command provides a stable fetch-style CLI surface and delegates
 * transfer work to curl when available.
 */

#include <stddef.h>

extern int printf(const char *fmt, ...);
extern void _exit(int status);
extern int strcmp(const char *a, const char *b);
extern int open(const char *pathname, int flags, int mode);
extern int close(int fd);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);

#define O_RDONLY 0

static int file_exists(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

static void usage(void) {
    printf("Usage: fetch [-o output_file] [--ca-cert bundle.pem] <url>\n");
}

static __attribute__((used)) void fetch_main(int argc, char **argv) {
    const char *out = NULL;
    const char *url = NULL;
    const char *ca = NULL;
    const char *curl_path = NULL;
    char *args[16];
    int ai = 0;
    char *envp[2];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage();
                _exit(1);
            }
            out = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--ca-cert") == 0 || strcmp(argv[i], "--cacert") == 0) {
            if (i + 1 >= argc) {
                usage();
                _exit(1);
            }
            ca = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            usage();
            _exit(1);
        }
        url = argv[i];
    }

    if (!url) {
        usage();
        _exit(1);
    }

    if (file_exists("/bin/curl")) curl_path = "/bin/curl";
    else if (file_exists("/usr/bin/curl")) curl_path = "/usr/bin/curl";
    if (!curl_path) {
        printf("fetch: curl backend not found\n");
        _exit(1);
    }

    args[ai++] = "curl";
    args[ai++] = "-f";
    args[ai++] = "-s";
    args[ai++] = "-S";
    if (ca) {
        args[ai++] = "--cacert";
        args[ai++] = (char *)ca;
    }
    if (out) {
        args[ai++] = "-o";
        args[ai++] = (char *)out;
    }
    args[ai++] = (char *)url;
    args[ai] = NULL;

    envp[0] = "PATH=/bin:/sbin:/usr/bin";
    envp[1] = NULL;
    execve(curl_path, args, envp);
    printf("fetch: failed to exec curl backend\n");
    _exit(127);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "andq $-16, %rsp\n"
        "call fetch_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}
