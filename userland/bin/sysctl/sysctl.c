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

void _start(int argc, char *argv[]) {
    int show_all = 0;
    int i;
    
    if (argc < 2) {
        print_usage();
        _exit(1);
    }
    
    /* Parse options */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'a':
                    show_all = 1;
                    break;
                case 'w':
                    /* Compatibility flag: writes are inferred from name=value */
                    break;
                case 'h':
                    print_usage();
                    _exit(0);
                    break;
                default:
                    printf("Unknown option: %s\n", argv[i]);
                    _exit(1);
            }
        } else {
            break;
        }
    }
    
    if (show_all) {
        /* TODO: List all sysctl variables */
        printf("sysctl: -a not yet implemented\n");
        _exit(0);
    }
    
    /* Process variables */
    for (; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        
        if (eq) {
            /* Write mode */
            *eq = '\0';
            sysctl_write(argv[i], eq + 1);
        } else {
            /* Read mode */
            sysctl_read(argv[i]);
        }
    }
    
    _exit(0);
}