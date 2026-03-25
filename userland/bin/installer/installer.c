/*
 * Obelisk OS - OpenBSD-style CLI Installer
 * From Axioms, Order.
 */

#include <stddef.h>

typedef long ssize_t;
typedef enum {
    DESKTOP_TTY = 0,
    DESKTOP_XORG = 1,
    DESKTOP_XFCE = 2,
    DESKTOP_XDM = 3
} desktop_mode_t;

extern int printf(const char *fmt, ...);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int open(const char *pathname, int flags, int mode);
extern int close(int fd);
extern int mkdir(const char *pathname, int mode);
extern void _exit(int status);
extern int fork(void);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);
extern int waitpid(int pid, int *status, int options);
extern int strcmp(const char *s1, const char *s2);
extern char *strcpy(char *dest, const char *src);
extern size_t strlen(const char *s);

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_CREAT  0x0040
#define O_TRUNC  0x0200

static void trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 &&
           (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

static void prompt_raw(const char *q, char *buf, size_t cap) {
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
    trim(buf);
}

static void prompt_default(const char *q, const char *defv, char *buf, size_t cap) {
    prompt_raw(q, buf, cap);
    if (buf[0] == '\0' && defv) {
        strcpy(buf, defv);
    }
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int is_yes(const char *s) {
    return strcmp(s, "yes") == 0 || strcmp(s, "y") == 0 || strcmp(s, "Y") == 0;
}

static int is_no(const char *s) {
    return strcmp(s, "no") == 0 || strcmp(s, "n") == 0 || strcmp(s, "N") == 0;
}

static int is_valid_hostname(const char *s) {
    size_t i = 0;
    if (!s || s[0] == '\0') return 0;
    while (s[i]) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok) return 0;
        i++;
    }
    return 1;
}

static int write_all(int fd, const char *s) {
    size_t left = strlen(s);
    const char *p = s;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0) return -1;
        p += (size_t)n;
        left -= (size_t)n;
    }
    return 0;
}

static int write_buffer(int fd, const char *buf, size_t len) {
    const char *p = buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0) return -1;
        p += (size_t)n;
        left -= (size_t)n;
    }
    return 0;
}

static void path_join(char *out, size_t cap, const char *root, const char *leaf) {
    size_t i = 0;
    if (cap == 0) return;
    out[0] = '\0';
    while (i + 1 < cap && root[i]) {
        out[i] = root[i];
        i++;
    }
    out[i] = '\0';
    if (i > 0 && out[i - 1] != '/' && i + 1 < cap) {
        out[i++] = '/';
        out[i] = '\0';
    }
    if (leaf[0] == '/') leaf++;
    {
        size_t b = 0;
        while (i + b + 1 < cap && leaf[b]) {
            out[i + b] = leaf[b];
            b++;
        }
        out[i + b] = '\0';
    }
}

static int mkdir_p(const char *path, int mode) {
    char tmp[320];
    size_t i = 0;
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return -1;
    while (i < n) {
        tmp[i] = path[i];
        i++;
    }
    tmp[n] = '\0';
    for (i = 1; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0] != '\0') (void)mkdir(tmp, mode);
            tmp[i] = '/';
        }
    }
    (void)mkdir(tmp, mode);
    return 0;
}

static int copy_file(const char *src, const char *dst) {
    char buf[4096];
    int in_fd = open(src, O_RDONLY, 0);
    int out_fd;
    if (in_fd < 0) return -1;
    out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }
    for (;;) {
        ssize_t n = read(in_fd, buf, sizeof(buf));
        if (n < 0) {
            close(in_fd);
            close(out_fd);
            return -1;
        }
        if (n == 0) break;
        if (write_buffer(out_fd, buf, (size_t)n) < 0) {
            close(in_fd);
            close(out_fd);
            return -1;
        }
    }
    close(in_fd);
    close(out_fd);
    return 0;
}

static int run_exec(const char *prog, char *const argv[]) {
    int pid = fork();
    int status = 0;
    if (pid < 0) return -1;
    if (pid == 0) {
        char *envp[] = { "PATH=/bin:/sbin:/usr/bin", NULL };
        (void)execve(prog, argv, envp);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) { }
    return status == 0 ? 0 : -1;
}

static int write_repos_conf(const char *path, const char *repo_url, const char *repo_pin) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (write_all(fd, "core file:///var/cache/opkg/repo\n") < 0) {
        close(fd);
        return -1;
    }
    if (repo_url && repo_url[0]) {
        if (write_all(fd, "desktop ") < 0 || write_all(fd, repo_url) < 0) {
            close(fd);
            return -1;
        }
        if (repo_pin && repo_pin[0]) {
            if (write_all(fd, " ") < 0 || write_all(fd, repo_pin) < 0) {
                close(fd);
                return -1;
            }
        }
        if (write_all(fd, "\n") < 0) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    return 0;
}

static int write_install_plan(const char *path,
                              const char *target_disk,
                              const char *hostname,
                              int proc_enabled,
                              const char *target_root,
                              desktop_mode_t desktop_mode,
                              const char *repo_url,
                              const char *repo_pin) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (write_all(fd, "# Obelisk installer plan\n") < 0 ||
        write_all(fd, "target_disk=") < 0 || write_all(fd, target_disk) < 0 ||
        write_all(fd, "\nhostname=") < 0 || write_all(fd, hostname) < 0 ||
        write_all(fd, "\nproc_compat=") < 0 || write_all(fd, proc_enabled ? "yes\n" : "no\n") < 0 ||
        write_all(fd, "target_root=") < 0 || write_all(fd, target_root) < 0 ||
        write_all(fd, "\ndesktop_mode=") < 0 ||
        write_all(fd, desktop_mode == DESKTOP_XDM ? "xdm\n" :
                      desktop_mode == DESKTOP_XFCE ? "xfce\n" :
                      desktop_mode == DESKTOP_XORG ? "xorg\n" : "tty\n") < 0 ||
        write_all(fd, "repo_url=") < 0 || write_all(fd, (repo_url && repo_url[0]) ? repo_url : "(none)") < 0 ||
        write_all(fd, "\nrepo_pin=") < 0 || write_all(fd, (repo_pin && repo_pin[0]) ? repo_pin : "(none)") < 0 ||
        write_all(fd, "\n") < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int perform_staging_install(const char *target_root,
                                   const char *target_disk,
                                   const char *hostname,
                                   int proc_enabled,
                                   desktop_mode_t desktop_mode,
                                   const char *repo_url,
                                   const char *repo_pin) {
    char p[320];
    char dst[320];
    char conf_path[320];
    int fd;

    if (mkdir_p(target_root, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "sbin"); if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "etc"); if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "etc/opkg"); if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "etc/axiomd"); if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "etc/axiomd/policy"); if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "var"); if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "tmp"); if (mkdir_p(p, 0777) < 0) return -1;
    path_join(p, sizeof(p), target_root, "dev"); if (mkdir_p(p, 0755) < 0) return -1;

    path_join(dst, sizeof(dst), target_root, "sbin/init"); if (copy_file("/sbin/init", dst) < 0) return -1;
    path_join(dst, sizeof(dst), target_root, "sbin/sysctl"); if (copy_file("/sbin/sysctl", dst) < 0) return -1;
    path_join(dst, sizeof(dst), target_root, "sbin/installer"); if (copy_file("/sbin/installer", dst) < 0) return -1;
    path_join(dst, sizeof(dst), target_root, "sbin/installer-tui"); if (copy_file("/sbin/installer-tui", dst) < 0) return -1;

    path_join(dst, sizeof(dst), target_root, "etc/axiomd/main.pro"); (void)copy_file("/etc/axiomd/main.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/kernel_ipc.pro"); (void)copy_file("/etc/axiomd/kernel_ipc.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/sandbox.pro"); (void)copy_file("/etc/axiomd/sandbox.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/policy/access.pro"); (void)copy_file("/etc/axiomd/policy/access.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/policy/allocation.pro"); (void)copy_file("/etc/axiomd/policy/allocation.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/policy/inheritance.pro"); (void)copy_file("/etc/axiomd/policy/inheritance.pro", dst);

    path_join(p, sizeof(p), target_root, "etc/hostname");
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (write_all(fd, hostname) < 0 || write_all(fd, "\n") < 0) {
        close(fd);
        return -1;
    }
    close(fd);

    if (write_install_plan("/etc/obelisk-install.conf", target_disk, hostname, proc_enabled,
                           target_root, desktop_mode, repo_url, repo_pin) < 0) return -1;
    path_join(conf_path, sizeof(conf_path), target_root, "etc/obelisk-install.conf");
    if (write_install_plan(conf_path, target_disk, hostname, proc_enabled,
                           target_root, desktop_mode, repo_url, repo_pin) < 0) return -1;

    path_join(conf_path, sizeof(conf_path), target_root, "etc/opkg/repos.conf");
    if (write_repos_conf(conf_path, repo_url, repo_pin) < 0) return -1;

    path_join(conf_path, sizeof(conf_path), target_root, "etc/obelisk-desktop.conf");
    fd = open(conf_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write_all(fd, "desktop_mode=");
        write_all(fd, desktop_mode == DESKTOP_XDM ? "xdm\n" :
                      desktop_mode == DESKTOP_XFCE ? "xfce\n" :
                      desktop_mode == DESKTOP_XORG ? "xorg\n" : "tty\n");
        if (repo_url && repo_url[0]) {
            write_all(fd, "desktop_repo_url=");
            write_all(fd, repo_url);
            write_all(fd, "\n");
            if (repo_pin && repo_pin[0]) {
                write_all(fd, "desktop_repo_pin=");
                write_all(fd, repo_pin);
                write_all(fd, "\n");
            }
        }
        close(fd);
    }

    if (desktop_mode != DESKTOP_TTY) {
        path_join(conf_path, sizeof(conf_path), target_root, "etc/obelisk-package-profiles.conf");
        fd = open(conf_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return -1;
        if (write_all(fd, "# Desktop profile requests staged by installer\n") < 0 ||
            write_all(fd, "xorg\n") < 0 ||
            ((desktop_mode == DESKTOP_XFCE || desktop_mode == DESKTOP_XDM) && write_all(fd, "xfce\n") < 0) ||
            (desktop_mode == DESKTOP_XDM && write_all(fd, "xdm\n") < 0)) {
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int run_opkg_desktop_install(desktop_mode_t mode, const char *repo_url, const char *repo_pin) {
    int tries;
    if (mode == DESKTOP_TTY) return 0;

    if (write_repos_conf("/etc/opkg/repos.conf", repo_url, repo_pin) < 0) {
        printf("Warning: could not update /etc/opkg/repos.conf in live environment.\n");
    }

    printf("\nResolving selected desktop profiles via opkg...\n");
    for (tries = 0; tries < 3; tries++) {
        char *update_argv[] = { "/bin/opkg", "update", NULL };
        if (run_exec("/bin/opkg", update_argv) == 0) break;
        printf("  opkg update attempt %d/3 failed\n", tries + 1);
    }
    if (tries == 3) {
        printf("Warning: opkg update failed; desktop profile install deferred.\n");
        return -1;
    }
    {
        char *xorg_argv[] = { "/bin/opkg", "install-profile", "xorg", NULL };
        if (run_exec("/bin/opkg", xorg_argv) < 0) {
            printf("Warning: failed to install xorg profile (retry post-install).\n");
            return -1;
        }
    }
    if (mode == DESKTOP_XFCE || mode == DESKTOP_XDM) {
        char *xfce_argv[] = { "/bin/opkg", "install-profile", "xfce", NULL };
        if (run_exec("/bin/opkg", xfce_argv) < 0) {
            printf("Warning: failed to install xfce profile (retry post-install).\n");
            return -1;
        }
    }
    if (mode == DESKTOP_XDM) {
        char *xdm_argv[] = { "/bin/opkg", "install-profile", "xdm", NULL };
        if (run_exec("/bin/opkg", xdm_argv) < 0) {
            printf("Warning: failed to install xdm profile (compat fallback may still work).\n");
            return -1;
        }
    }
    return 0;
}

void _start(void) {
    char target_disk[96];
    char target_root[160];
    char hostname[96];
    char yn[16];
    char desktop_sel[16];
    char repo_url[256];
    char repo_pin[128];
    char confirm[16];
    int proc_enabled = 0;
    desktop_mode_t desktop_mode = DESKTOP_TTY;

    printf("\nObelisk Installer (CLI)\n");
    printf("=======================\n");
    printf("This installer stages an install root, writes persistent config,\n");
    printf("and can bootstrap desktop profiles from local/web opkg repos.\n");
    printf("Disk partitioning, mkfs, and bootloader writes remain explicit operator steps.\n\n");

    for (;;) {
        prompt_default("Target disk [/dev/sda]: ", "/dev/sda", target_disk, sizeof(target_disk));
        if (starts_with(target_disk, "/dev/")) break;
        printf("  Invalid path. Expected /dev/<disk>.\n");
    }
    prompt_default("Target root [/mnt/obelisk]: ", "/mnt/obelisk", target_root, sizeof(target_root));
    for (;;) {
        prompt_default("Hostname [obelisk]: ", "obelisk", hostname, sizeof(hostname));
        if (is_valid_hostname(hostname)) break;
        printf("  Invalid hostname. Use letters, numbers, '-' or '.'.\n");
    }
    for (;;) {
        prompt_default("Enable /proc compatibility? [yes/no] (no): ", "no", yn, sizeof(yn));
        if (is_yes(yn)) { proc_enabled = 1; break; }
        if (is_no(yn)) { proc_enabled = 0; break; }
        printf("  Please answer yes or no.\n");
    }

    printf("\nDesktop mode:\n");
    printf("  tty  - stable text-only system\n");
    printf("  xorg - X11 base stack only\n");
    printf("  xfce - full XFCE profile (implies xorg)\n");
    printf("  xdm  - display-manager style boot (uses XFCE/Xorg path)\n");
    for (;;) {
        prompt_default("Choose desktop mode [tty/xorg/xfce/xdm] (tty): ", "tty", desktop_sel, sizeof(desktop_sel));
        if (strcmp(desktop_sel, "tty") == 0) { desktop_mode = DESKTOP_TTY; break; }
        if (strcmp(desktop_sel, "xorg") == 0) { desktop_mode = DESKTOP_XORG; break; }
        if (strcmp(desktop_sel, "xfce") == 0) { desktop_mode = DESKTOP_XFCE; break; }
        if (strcmp(desktop_sel, "xdm") == 0) { desktop_mode = DESKTOP_XDM; break; }
        printf("  Unknown mode. Pick tty, xorg, xfce, or xdm.\n");
    }

    repo_url[0] = '\0';
    repo_pin[0] = '\0';
    if (desktop_mode != DESKTOP_TTY) {
        printf("\nOptional desktop web repository:\n");
        printf("  Leave blank to use only bundled/local repos.\n");
        prompt_raw("Desktop repo URL [https://... or file://...]: ", repo_url, sizeof(repo_url));
        if (repo_url[0]) {
            prompt_raw("Optional index pin [sha256:<hash>] (blank to skip): ", repo_pin, sizeof(repo_pin));
        }
    }

    printf("\nInstall plan:\n");
    printf("  disk:         %s\n", target_disk);
    printf("  root:         %s\n", target_root);
    printf("  hostname:     %s\n", hostname);
    printf("  /proc:        %s\n", proc_enabled ? "yes" : "no");
    printf("  desktop:      %s\n", desktop_mode == DESKTOP_XDM ? "xdm" :
                                     desktop_mode == DESKTOP_XFCE ? "xfce" :
                                     desktop_mode == DESKTOP_XORG ? "xorg" : "tty");
    printf("  desktop repo: %s\n", repo_url[0] ? repo_url : "(none)");
    if (repo_pin[0]) printf("  index pin:    %s\n", repo_pin);

    prompt_default("Proceed with staging install now? [yes/no] (no): ", "no", confirm, sizeof(confirm));
    if (!is_yes(confirm)) {
        printf("Aborted by user.\n");
        _exit(1);
    }

    if (perform_staging_install(target_root, target_disk, hostname, proc_enabled, desktop_mode, repo_url, repo_pin) < 0) {
        printf("Failed to stage install tree.\n");
        _exit(1);
    }
    (void)run_opkg_desktop_install(desktop_mode, repo_url, repo_pin);

    printf("\nInstall staging complete.\n");
    printf("  - live plan:   /etc/obelisk-install.conf\n");
    printf("  - target plan: %s/etc/obelisk-install.conf\n", target_root);
    printf("  - target repo: %s/etc/opkg/repos.conf\n", target_root);
    printf("Next step: mount real target FS there, rerun installer if needed, then install bootloader.\n");
    _exit(0);
}
