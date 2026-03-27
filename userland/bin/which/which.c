/*
 * Obelisk — which(1): first executable on PATH for each name.
 */

#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int access(const char *pathname, int mode);
extern void *memcpy(void *d, const void *s, size_t n);

static void wrerr(const char *s) {
    (void)write(2, s, strlen(s));
}

#define X_OK 1
#define PATH_DEF "/bin:/sbin:/usr/bin"

static const char *path_from_env(char **envp) {
    size_t i;

    if (!envp) {
        return PATH_DEF;
    }
    for (i = 0; envp[i]; i++) {
        if (strncmp(envp[i], "PATH=", 5) == 0) {
            return envp[i] + 5;
        }
    }
    return PATH_DEF;
}

static int try_one(const char *dir, size_t dlen, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name);

    if (dlen + 1 + nl + 1 >= cap) {
        return -1;
    }
    memcpy(out, dir, dlen);
    if (dlen == 0 || out[dlen - 1] != '/') {
        out[dlen++] = '/';
    }
    memcpy(out + dlen, name, nl + 1);
    if (access(out, X_OK) == 0) {
        return 0;
    }
    return -1;
}

static int which_name(const char *name, char **envp, char *buf, size_t cap) {
    const char *path = path_from_env(envp);
    const char *p;
    const char *start;

    if (!name || !name[0]) {
        return -1;
    }
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0) {
            strncpy(buf, name, cap - 1);
            buf[cap - 1] = '\0';
            return 0;
        }
        return -1;
    }
    p = path;
    while (*p) {
        start = p;
        while (*p && *p != ':') {
            p++;
        }
        if (try_one(start, (size_t)(p - start), name, buf, cap) == 0) {
            return 0;
        }
        if (*p == ':') {
            p++;
        }
    }
    return -1;
}

int main(int argc, char **argv, char **envp) {
    char out[512];
    int i;
    int miss = 0;

    if (argc < 2) {
        wrerr("usage: which name [...]\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (which_name(argv[i], envp, out, sizeof(out)) < 0) {
            miss = 1;
            continue;
        }
        (void)write(1, out, strlen(out));
        (void)write(1, "\n", 1);
    }
    return miss ? 1 : 0;
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "lea 8(%rsi,%rdi,8), %rax\n"
        "lea 8(%rax), %rdx\n"
        "andq $-16, %rsp\n"
        "call main\n"
        "mov %eax, %edi\n"
        "call _exit\n");
}
