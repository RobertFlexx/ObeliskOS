/*
 * Obelisk OS - FreeBSD-style TUI Installer
 * From Axioms, Order.
 */

#include <stddef.h>

typedef long ssize_t;
extern int printf(const char *fmt, ...);
extern ssize_t read(int fd, void *buf, size_t count);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern void _exit(int status);

static void draw_box(void) {
    printf("+-------------------------------------------+\n");
    printf("|           Obelisk TUI Installer           |\n");
    printf("+-------------------------------------------+\n");
    printf("| 1) Guided install                          |\n");
    printf("| 2) Shell-style CLI install                 |\n");
    printf("| q) Quit                                    |\n");
    printf("+-------------------------------------------+\n");
}

static void prompt(const char *q, char *buf, size_t cap) {
    size_t i = 0;
    char ch = 0;
    printf("%s", q);
    while (i + 1 < cap) {
        if (read(0, &ch, 1) <= 0) break;
        if (ch == '\r') continue;
        if (ch == '\n') break;
        buf[i++] = ch;
    }
    buf[i] = '\0';
}

static int read_choice(void) {
    char c = 0;
    if (read(0, &c, 1) <= 0) {
        return -1;
    }
    return (int)c;
}

void _start(void) {
    for (;;) {
        draw_box();
        printf("Select option: ");

        int ch = read_choice();
        printf("\n");

        if (ch == '1') {
            char disk[64];
            char host[64];
            char proc[8];
            printf("Guided mode selected.\n");
            printf("This mode collects settings, then launches CLI installer.\n\n");
            prompt("Disk (/dev/sda): ", disk, sizeof(disk));
            prompt("Hostname (obelisk): ", host, sizeof(host));
            prompt("Enable /proc [yes/no]: ", proc, sizeof(proc));
            printf("\nCollected:\n  disk=%s\n  hostname=%s\n  /proc=%s\n\n",
                   disk[0] ? disk : "/dev/sda",
                   host[0] ? host : "obelisk",
                   proc[0] ? proc : "no");

            char *argv[] = { "/sbin/installer", NULL };
            char *envp[] = { NULL };
            (void)execve("/sbin/installer", argv, envp);
            printf("Failed to launch /sbin/installer\n");
            _exit(1);
        } else if (ch == '2') {
            char *argv[] = { "/sbin/installer", NULL };
            char *envp[] = { NULL };
            (void)execve("/sbin/installer", argv, envp);
            printf("Failed to launch /sbin/installer\n");
            _exit(1);
        } else if (ch == 'q' || ch == 'Q') {
            printf("Installer aborted.\n");
            _exit(0);
        } else {
            printf("Unknown selection.\n\n");
        }
    }
}
