/*
 * Obelisk OS - desktop-session launcher
 * From Axioms, Order.
 */

#include <stddef.h>

extern int printf(const char *fmt, ...);
extern int open(const char *pathname, int flags, int mode);
extern int close(int fd);
extern int fork(void);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int waitpid(int pid, int *status, int options);
extern int strcmp(const char *a, const char *b);
extern int _exit(int status);

#define O_RDONLY 0x0000

enum target_mode {
    TARGET_AUTO = 0,
    TARGET_XORG = 1,
    TARGET_XFCE = 2,
    TARGET_XDM  = 3,
};

static int file_exists(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

static int is_script_launcher(const char *path) {
    if (!path) return 0;
    if (strcmp(path, "/usr/lib/obelisk/desktop/xdm-compat") == 0) return 1;
    if (strcmp(path, "/usr/lib/obelisk/desktop/startxfce4-compat") == 0) return 1;
    if (strcmp(path, "/usr/lib/obelisk/desktop/xfce4-session-compat") == 0) return 1;
    if (strcmp(path, "/usr/lib/obelisk/desktop/xinit-compat") == 0) return 1;
    if (strcmp(path, "/usr/lib/obelisk/desktop/Xorg-compat") == 0) return 1;
    return 0;
}

static int run_wait(const char *path, char *const argv[]) {
    int pid = fork();
    int st = 0;
    if (pid < 0) return -1;
    if (pid == 0) {
        char *envp[] = {
            "PATH=/bin:/sbin:/usr/bin",
            "HOME=/home/obelisk",
            "TERM=vt100",
            "USER=obelisk",
            "SHELL=/bin/osh",
            "XDG_SESSION_TYPE=x11",
            0
        };
        execve(path, argv, envp);
        _exit(127);
    }
    while (waitpid(pid, &st, 0) < 0) { }
    return st == 0 ? 0 : -1;
}

static const char *find_xserver_bin(void) {
    if (file_exists("/usr/lib/xorg/Xorg")) return "/usr/lib/xorg/Xorg";
    if (file_exists("/usr/bin/Xorg")) return "/usr/bin/Xorg";
    if (file_exists("/bin/Xorg")) return "/bin/Xorg";
    if (file_exists("/usr/bin/X")) return "/usr/bin/X";
    if (file_exists("/bin/X")) return "/bin/X";
    return 0;
}

static void try_exec_direct_xfce(char *const envp[]) {
    const char *xinit = 0;
    const char *client = 0;
    const char *xserver = find_xserver_bin();

    if (file_exists("/usr/bin/xinit")) xinit = "/usr/bin/xinit";
    else if (file_exists("/bin/xinit")) xinit = "/bin/xinit";

    if (file_exists("/usr/bin/xfce4-session")) client = "/usr/bin/xfce4-session";
    else if (file_exists("/bin/xfce4-session")) client = "/bin/xfce4-session";
    else if (file_exists("/usr/bin/startxfce4")) client = "/usr/bin/startxfce4";
    else if (file_exists("/bin/startxfce4")) client = "/bin/startxfce4";

    if (xinit && client && xserver) {
        char *args[] = {
            (char *)xinit,
            (char *)client,
            "--",
            (char *)xserver,
            ":0",
            "-nolisten",
            "tcp",
            0
        };
        printf("desktop-session: launching direct XFCE stack via %s\n", xinit);
        execve(xinit, args, envp);
    }
}

static enum target_mode pick_target(void) {
    int have_xfce = file_exists("/var/lib/opkg/installed/xfce.json") ||
                    file_exists("/var/lib/opkg/installed/xfce4.json") ||
                    file_exists("/var/lib/opkg/installed/xfce-desktop.json");
    int have_xorg = file_exists("/var/lib/opkg/installed/xorg.json") ||
                    file_exists("/var/lib/opkg/installed/xorg-base.json") ||
                    file_exists("/var/lib/opkg/installed/xorg-server.json");
    int have_xdm = file_exists("/var/lib/opkg/installed/xdm.json") ||
                   file_exists("/usr/bin/xdm") ||
                   file_exists("/bin/xdm") ||
                   file_exists("/usr/lib/obelisk/desktop/xdm-compat");
    if (have_xdm && have_xfce) return TARGET_XDM;
    if (have_xfce) return TARGET_XFCE;
    if (have_xorg) return TARGET_XORG;
    return TARGET_AUTO;
}

static int require_profile(enum target_mode target) {
    if (target == TARGET_XFCE || target == TARGET_XDM) {
        if (!file_exists("/var/lib/opkg/installed/xfce.json") &&
            !file_exists("/var/lib/opkg/installed/xfce4.json") &&
            !file_exists("/var/lib/opkg/installed/xfce-desktop.json")) {
            printf("desktop-session: xfce profile not installed (run: opkg install-profile xfce)\n");
            return -1;
        }
    }
    if (target == TARGET_XORG || target == TARGET_XFCE || target == TARGET_XDM) {
        if (!file_exists("/var/lib/opkg/installed/xorg.json") &&
            !file_exists("/var/lib/opkg/installed/xorg-base.json") &&
            !file_exists("/var/lib/opkg/installed/xorg-server.json")) {
            printf("desktop-session: xorg profile not installed (run: opkg install-profile xorg)\n");
            return -1;
        }
    }
    return 0;
}

static const char *find_xdm_launcher(void) {
    static const char *cand[] = {
        "/usr/lib/obelisk/desktop/xdm-compat",
        "/usr/bin/xdm",
        "/bin/xdm"
    };
    int i;
    for (i = 0; i < (int)(sizeof(cand) / sizeof(cand[0])); i++) {
        if (file_exists(cand[i])) return cand[i];
    }
    return 0;
}

static const char *find_xfce_launcher(void) {
    static const char *cand[] = {
        "/usr/bin/startxfce4",
        "/bin/startxfce4",
        "/usr/bin/xfce4-session",
        "/bin/xfce4-session",
        "/usr/lib/obelisk/desktop/startxfce4-compat",
        "/usr/lib/obelisk/desktop/xfce4-session-compat"
    };
    int i;
    for (i = 0; i < (int)(sizeof(cand) / sizeof(cand[0])); i++) {
        if (file_exists(cand[i])) return cand[i];
    }
    return 0;
}

static const char *find_xorg_launcher(void) {
    static const char *cand[] = {
        "/usr/bin/xinit",
        "/bin/xinit",
        "/usr/bin/Xorg",
        "/bin/Xorg",
        "/usr/lib/obelisk/desktop/xinit-compat",
        "/usr/lib/obelisk/desktop/Xorg-compat"
    };
    int i;
    for (i = 0; i < (int)(sizeof(cand) / sizeof(cand[0])); i++) {
        if (file_exists(cand[i])) return cand[i];
    }
    return 0;
}

static void print_usage(void) {
    printf("usage: desktop-session [auto|xorg|xfce|xdm] [--probe-only]\n");
}

static __attribute__((used)) void desktop_session_main(int argc, char **argv) {
    enum target_mode target = TARGET_AUTO;
    int probe_only = 0;
    const char *launcher = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--probe-only") == 0) {
            probe_only = 1;
        } else if (strcmp(argv[i], "auto") == 0) {
            target = TARGET_AUTO;
        } else if (strcmp(argv[i], "xorg") == 0) {
            target = TARGET_XORG;
        } else if (strcmp(argv[i], "xfce") == 0) {
            target = TARGET_XFCE;
        } else if (strcmp(argv[i], "xdm") == 0) {
            target = TARGET_XDM;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            _exit(0);
        } else {
            print_usage();
            _exit(1);
        }
    }

    printf("desktop-session: validating graphics/input stack...\n");
    {
        char *smoke_argv[] = { "xorg-smoke", 0 };
        if (run_wait("/bin/xorg-smoke", smoke_argv) < 0) {
            printf("desktop-session: xorg-smoke failed\n");
            _exit(2);
        }
    }

    if (target == TARGET_AUTO) {
        target = pick_target();
        if (target == TARGET_AUTO) {
            printf("desktop-session: no desktop profile installed, defaulting to xorg mode\n");
            target = TARGET_XORG;
        }
    }

    if (require_profile(target) < 0) {
        _exit(3);
    }

    if (target == TARGET_XDM) {
        launcher = find_xdm_launcher();
        if (!launcher) {
            printf("desktop-session: xdm launcher missing (expected xdm or xdm-compat)\n");
            _exit(4);
        }
        printf("desktop-session: selected xdm launcher: %s\n", launcher);
    } else if (target == TARGET_XFCE) {
        launcher = find_xfce_launcher();
        if (!launcher) {
            printf("desktop-session: xfce launcher missing (expected startxfce4/xfce4-session or compat wrapper)\n");
            _exit(4);
        }
        printf("desktop-session: selected xfce launcher: %s\n", launcher);
    } else {
        launcher = find_xorg_launcher();
        if (!launcher) {
            printf("desktop-session: xorg launcher missing (expected xinit/Xorg or compat wrapper)\n");
            _exit(4);
        }
        printf("desktop-session: selected xorg launcher: %s\n", launcher);
    }

    if (probe_only) {
        printf("desktop-session: probe passed\n");
        _exit(0);
    }

    if (target == TARGET_XDM) {
        char *envp[] = {
            "PATH=/bin:/sbin:/usr/bin",
            "HOME=/home/obelisk",
            "TERM=vt100",
            "USER=obelisk",
            "SHELL=/bin/osh",
            "XDG_SESSION_TYPE=x11",
            0
        };
        /* Prefer direct launch to avoid dependence on shell-script grammar. */
        try_exec_direct_xfce(envp);
        if (strcmp(launcher, "/usr/bin/xdm") == 0 || strcmp(launcher, "/bin/xdm") == 0) {
            char *args[] = { (char *)launcher, "-nodaemon", "-config", "/etc/X11/xdm/xdm-config", 0 };
            execve(launcher, args, envp);
        } else {
            if (is_script_launcher(launcher)) {
                char *args[] = { "/bin/sh", (char *)launcher, 0 };
                execve("/bin/sh", args, envp);
            } else {
                char *args[] = { (char *)launcher, 0 };
                execve(launcher, args, envp);
            }
        }
        printf("desktop-session: failed to exec %s\n", launcher);
        _exit(5);
    } else if (target == TARGET_XFCE) {
        char *envp[] = {
            "PATH=/bin:/sbin:/usr/bin",
            "HOME=/home/obelisk",
            "TERM=vt100",
            "USER=obelisk",
            "SHELL=/bin/osh",
            "XDG_SESSION_TYPE=x11",
            0
        };
        /* Prefer direct launch to avoid dependence on shell-script grammar. */
        try_exec_direct_xfce(envp);
        if (is_script_launcher(launcher)) {
            char *args[] = { "/bin/sh", (char *)launcher, 0 };
            execve("/bin/sh", args, envp);
        } else {
            char *args[] = { (char *)launcher, 0 };
            execve(launcher, args, envp);
        }
        printf("desktop-session: failed to exec %s\n", launcher);
        _exit(5);
    } else {
        if (strcmp(launcher, "/usr/bin/Xorg") == 0 || strcmp(launcher, "/bin/Xorg") == 0) {
            char *args[] = { (char *)launcher, ":0", "-retro", "-nolisten", "tcp", 0 };
            char *envp[] = {
                "PATH=/bin:/sbin:/usr/bin",
                "HOME=/home/obelisk",
                "TERM=vt100",
                "USER=obelisk",
                "SHELL=/bin/osh",
                "XDG_SESSION_TYPE=x11",
                0
            };
            execve(launcher, args, envp);
            printf("desktop-session: failed to exec %s\n", launcher);
            _exit(5);
        } else {
            char *envp[] = {
                "PATH=/bin:/sbin:/usr/bin",
                "HOME=/home/obelisk",
                "TERM=vt100",
                "USER=obelisk",
                "SHELL=/bin/osh",
                "XDG_SESSION_TYPE=x11",
                0
            };
            if (is_script_launcher(launcher)) {
                char *args[] = { "/bin/sh", (char *)launcher, 0 };
                execve("/bin/sh", args, envp);
            } else {
                char *args[] = { (char *)launcher, 0 };
                execve(launcher, args, envp);
            }
            printf("desktop-session: failed to exec %s\n", launcher);
            _exit(5);
        }
    }
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "andq $-16, %rsp\n"
        "call desktop_session_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}
