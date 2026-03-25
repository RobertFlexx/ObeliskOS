/*
 * Obelisk OS - FreeBSD-style TUI Installer
 * From Axioms, Order.
 */

#include <stddef.h>

typedef long ssize_t;
extern int printf(const char *fmt, ...);
extern ssize_t read(int fd, void *buf, size_t count);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int strcmp(const char *a, const char *b);
extern void _exit(int status);

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

static void clear_screen(void) {
    printf("\033[2J\033[H");
}

static void draw_header(void) {
    printf("+------------------------------------------------------------+\n");
    printf("|                    Obelisk TUI Installer                   |\n");
    printf("+------------------------------------------------------------+\n");
}

static void pause_enter(void) {
    char b[8];
    prompt("Press Enter to continue...", b, sizeof(b));
}

static int launch_cli_installer(void) {
    char *argv[] = { "/sbin/installer", NULL };
    char *envp[] = { "PATH=/bin:/sbin:/usr/bin", NULL };
    (void)execve("/sbin/installer", argv, envp);
    printf("Failed to launch /sbin/installer\n");
    return -1;
}

void _start(void) {
    char sel[8];
    for (;;) {
        clear_screen();
        draw_header();
        printf("| 1) Guided install wizard                                   |\n");
        printf("| 2) Advanced CLI installer                                 |\n");
        printf("| 3) Desktop profile help (TTY/Xorg/XFCE/XDM)              |\n");
        printf("| q) Quit                                                   |\n");
        printf("+------------------------------------------------------------+\n");
        prompt("Select option: ", sel, sizeof(sel));
        printf("\n");

        if (strcmp(sel, "1") == 0) {
            clear_screen();
            draw_header();
            printf("Guided wizard launches the improved CLI workflow.\n\n");
            printf("It will walk you through:\n");
            printf("  - disk and target root selection\n");
            printf("  - hostname and /proc compatibility\n");
            printf("  - desktop mode: tty / xorg / xfce / xdm\n");
            printf("  - optional web repo URL + index pin\n");
            printf("  - profile installation with opkg\n\n");
            pause_enter();
            if (launch_cli_installer() < 0) _exit(1);
        } else if (strcmp(sel, "2") == 0) {
            if (launch_cli_installer() < 0) _exit(1);
        } else if (strcmp(sel, "3") == 0) {
            clear_screen();
            draw_header();
            printf("Desktop mode guidance:\n\n");
            printf("  tty  - keep text shell only, smallest footprint\n");
            printf("  xorg - install X11 base profile only\n");
            printf("  xfce - install XFCE profile (includes xorg)\n\n");
            printf("  xdm  - display-manager style launcher path (xfce/xorg)\n\n");
            printf("For custom web repos you can provide:\n");
            printf("  - desktop repo URL (https://... or file://...)\n");
            printf("  - optional index pin sha256:<hash>\n\n");
            printf("After install, validate desktop launch path with:\n");
            printf("  desktop-session auto --probe-only\n\n");
            pause_enter();
        } else if (strcmp(sel, "q") == 0 || strcmp(sel, "Q") == 0) {
            printf("Installer aborted.\n");
            _exit(0);
        } else {
            printf("Unknown selection.\n\n");
            pause_enter();
        }
    }
}
