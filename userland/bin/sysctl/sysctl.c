/*
 * Obelisk OS - sysctl Command
 * From Axioms, Order.
 */

#include <stdint.h>
#include <stddef.h>

typedef long ssize_t;

extern int printf(const char *fmt, ...);
extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int strcmp(const char *s1, const char *s2);
extern char *strchr(const char *s, int c);
extern size_t strlen(const char *s);

/* sysctl syscall argument structure */
struct sysctl_args {
    const char *name;
    void *oldval;
    size_t *oldlenp;
    const void *newval;
    size_t newlen;
};

extern int sysctl(void *args);

static void print_usage(void) {
    printf("Usage: sysctl [options] [variable[=value] ...]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -a        Display all variables\n");
    printf("  -w        Write mode\n");
    printf("  -h        Show this help\n");
    printf("\n");
    printf("Run 'sysctl -a' to list all known variables.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  sysctl system.kernel.version\n");
    printf("  sysctl -w system.kernel.hostname=myhost\n");
    printf("\n");
}

static int sysctl_read(const char *name) {
    char value[1024];
    size_t len = sizeof(value);
    
    struct sysctl_args args = {
        .name = name,
        .oldval = value,
        .oldlenp = &len,
        .newval = NULL,
        .newlen = 0
    };
    
    int ret = sysctl(&args);
    if (ret < 0) {
        printf("sysctl: %s: not found\n", name);
        return -1;
    }
    
    printf("%s = %s\n", name, value);
    return 0;
}

static int sysctl_write(const char *name, const char *value) {
    size_t len = strlen(value) + 1;
    
    struct sysctl_args args = {
        .name = name,
        .oldval = NULL,
        .oldlenp = NULL,
        .newval = value,
        .newlen = len
    };
    
    int ret = sysctl(&args);
    if (ret < 0) {
        printf("sysctl: %s: permission denied or not found\n", name);
        return -1;
    }
    
    printf("%s = %s\n", name, value);
    return 0;
}

static const char *const known_sysctls[] = {
    "system.kernel.version",
    "system.kernel.ostype",
    "system.kernel.boot_time",
    "system.kernel.panic_on_oops",
    "system.kernel.hostname",
    "system.kernel.domainname",
    "system.kernel.uptime",
    "system.kernel.uptime_ms",
    "system.kernel.context_switches",
    "system.cpu.count",
    "system.cpu.frequency",
    "system.cpu.vendor",
    "system.cpu.model",
    "system.memory.total",
    "system.memory.free",
    "system.memory.cached",
    "system.scheduler.policy",
    "system.scheduler.timeslice",
    "system.fs.axiomfs.cache_timeout",
    "system.fs.axiomfs.cache_size",
    "system.fs.axiomfs.daemon_timeout",
    "system.fs.axiomfs.policy_enabled",
    "system.debug.loader_trace_enabled",
    "system.debug.loader_trace_budget",
    "system.debug.loader_exec_debug_enabled",
    NULL
};

static int dump_known_sysctls(void) {
    int rc = 0;
    for (int i = 0; known_sysctls[i]; i++) {
        if (sysctl_read(known_sysctls[i]) < 0) {
            rc = 1;
        }
    }
    return rc;
}

static __attribute__((used)) void sysctl_main(int argc, char **argv) {
    int show_all = 0;
    int write_mode = 0;
    int rc = 0;
    int i = 1;
    int endopts = 0;

    if (!argv || argc <= 1) {
        print_usage();
        _exit(0);
    }

    while (i < argc) {
        const char *arg = argv[i];
        if (!endopts && arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "--") == 0) {
                endopts = 1;
                i++;
                continue;
            }
            for (size_t j = 1; arg[j] != '\0'; j++) {
                switch (arg[j]) {
                    case 'a':
                        show_all = 1;
                        break;
                    case 'w':
                        write_mode = 1;
                        break;
                    case 'h':
                        print_usage();
                        _exit(0);
                        break;
                    default:
                        printf("Unknown option: %s\n", arg);
                        _exit(1);
                        break;
                }
            }
            i++;
        } else {
            break;
        }
    }

    if (show_all) {
        rc = dump_known_sysctls();
        _exit(rc);
    }

    if (i >= argc) {
        if (write_mode) {
            printf("sysctl: -w requires at least one name=value assignment\n");
            _exit(1);
        }
        print_usage();
        _exit(1);
    }

    for (; i < argc; i++) {
        const char *arg = argv[i];
        const char *eq = strchr(arg, '=');

        if (eq) {
            char name[256];
            size_t nlen = (size_t)(eq - arg);
            if (nlen == 0 || nlen >= sizeof(name) || eq[1] == '\0') {
                printf("sysctl: invalid assignment: %s\n", arg);
                rc = 1;
                continue;
            }
            for (size_t k = 0; k < nlen; k++) {
                name[k] = arg[k];
            }
            name[nlen] = '\0';
            if (sysctl_write(name, eq + 1) < 0) {
                rc = 1;
            }
        } else {
            if (write_mode) {
                printf("sysctl: expected name=value with -w: %s\n", arg);
                rc = 1;
                continue;
            }
            if (sysctl_read(arg) < 0) {
                rc = 1;
            }
        }
    }

    _exit(rc);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"   /* argc */
        "lea 8(%rsp), %rsi\n"  /* argv */
        "andq $-16, %rsp\n"
        "call sysctl_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}