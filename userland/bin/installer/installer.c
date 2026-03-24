/*
 * Obelisk OS - OpenBSD-style CLI Installer
 * From Axioms, Order.
 */

#include <stddef.h>

typedef long ssize_t;

extern int printf(const char *fmt, ...);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int open(const char *pathname, int flags, int mode);
extern int close(int fd);
extern int mkdir(const char *pathname, int mode);
extern void _exit(int status);
extern int strcmp(const char *s1, const char *s2);
extern char *strcpy(char *dest, const char *src);
extern char *strcat(char *dest, const char *src);
extern size_t strlen(const char *s);

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_CREAT  0x0040
#define O_TRUNC  0x0200

static void trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static void prompt(const char *q, char *buf, size_t cap) {
    size_t i = 0;
    char ch = 0;
    printf("%s", q);

    while (i + 1 < cap) {
        if (read(0, &ch, 1) <= 0) {
            break;
        }
        if (ch == '\r') continue;
        if (ch == '\n') break;
        buf[i++] = ch;
    }
    buf[i] = '\0';
    trim(buf);
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
    size_t root_len = strlen(root);
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
    root_len = i;
    i = 0;
    while (root_len + i + 1 < cap && leaf[i]) {
        out[root_len + i] = leaf[i];
        i++;
    }
    out[root_len + i] = '\0';
}

static int mkdir_p(const char *path, int mode) {
    int ret = mkdir(path, mode);
    if (ret == 0) return 0;
    /* Ignore EEXIST-like failure for now in this tiny libc environment. */
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

static int write_install_plan(const char *path, const char *target, const char *hostname,
                              _Bool proc_enabled, const char *target_root) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    if (write_all(fd, "# Obelisk installer plan\n") < 0 ||
        write_all(fd, "target_disk=") < 0 ||
        write_all(fd, target) < 0 ||
        write_all(fd, "\nhostname=") < 0 ||
        write_all(fd, hostname) < 0 ||
        write_all(fd, "\nproc_compat=") < 0 ||
        write_all(fd, proc_enabled ? "yes\n" : "no\n") < 0 ||
        write_all(fd, "target_root=") < 0 ||
        write_all(fd, target_root) < 0 ||
        write_all(fd, "\n") < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int perform_staging_install(const char *target_root, const char *target_disk,
                                   const char *hostname, _Bool proc_enabled) {
    char p[256];
    char dst[256];
    char conf_path[256];
    int fd;

    /* Create directory layout */
    if (mkdir_p(target_root, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "sbin");
    if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "etc");
    if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "etc/axiomd");
    if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "etc/axiomd/policy");
    if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "var");
    if (mkdir_p(p, 0755) < 0) return -1;
    path_join(p, sizeof(p), target_root, "tmp");
    if (mkdir_p(p, 0777) < 0) return -1;
    path_join(p, sizeof(p), target_root, "dev");
    if (mkdir_p(p, 0755) < 0) return -1;

    /* Copy key runtime binaries */
    path_join(dst, sizeof(dst), target_root, "sbin/init");
    if (copy_file("/sbin/init", dst) < 0) return -1;
    path_join(dst, sizeof(dst), target_root, "sbin/sysctl");
    if (copy_file("/sbin/sysctl", dst) < 0) return -1;
    path_join(dst, sizeof(dst), target_root, "sbin/installer");
    if (copy_file("/sbin/installer", dst) < 0) return -1;
    path_join(dst, sizeof(dst), target_root, "sbin/installer-tui");
    if (copy_file("/sbin/installer-tui", dst) < 0) return -1;

    /* Copy policy files if present in live rootfs */
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/main.pro");
    (void)copy_file("/etc/axiomd/main.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/kernel_ipc.pro");
    (void)copy_file("/etc/axiomd/kernel_ipc.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/sandbox.pro");
    (void)copy_file("/etc/axiomd/sandbox.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/policy/access.pro");
    (void)copy_file("/etc/axiomd/policy/access.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/policy/allocation.pro");
    (void)copy_file("/etc/axiomd/policy/allocation.pro", dst);
    path_join(dst, sizeof(dst), target_root, "etc/axiomd/policy/inheritance.pro");
    (void)copy_file("/etc/axiomd/policy/inheritance.pro", dst);

    /* Write target hostname */
    path_join(p, sizeof(p), target_root, "etc/hostname");
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (write_all(fd, hostname) < 0 || write_all(fd, "\n") < 0) {
        close(fd);
        return -1;
    }
    close(fd);

    /* Write install plan into both live and target root */
    if (write_install_plan("/etc/obelisk-install.conf", target_disk, hostname,
                           proc_enabled, target_root) < 0) {
        return -1;
    }
    path_join(conf_path, sizeof(conf_path), target_root, "etc/obelisk-install.conf");
    if (write_install_plan(conf_path, target_disk, hostname, proc_enabled, target_root) < 0) {
        return -1;
    }

    return 0;
}

void _start(void) {
    char target[64];
    char target_root[128];
    char hostname[64];
    char proc_compat[8];
    _Bool proc_enabled = 0;
    char confirm[8];

    printf("\nObelisk Installer (CLI)\n");
    printf("-----------------------\n");
    printf("Target environment: ISO installer for bare metal and VM.\n");
    printf("Installer stages a complete target root tree and config.\n");
    printf("Disk partition/mkfs/bootloader write remains external.\n\n");

    prompt("Target disk (example: /dev/sda): ", target, sizeof(target));
    if (!starts_with(target, "/dev/")) {
        printf("\nInvalid disk path. Use a /dev/ path (example: /dev/sda).\n");
        _exit(1);
    }
    prompt("Target root path [/mnt/obelisk]: ", target_root, sizeof(target_root));
    if (target_root[0] == '\0') {
        strcpy(target_root, "/mnt/obelisk");
    }

    prompt("Hostname [obelisk]: ", hostname, sizeof(hostname));
    if (hostname[0] == '\0') {
        hostname[0] = 'o'; hostname[1] = 'b'; hostname[2] = 'e';
        hostname[3] = 'l'; hostname[4] = 'i'; hostname[5] = 's';
        hostname[6] = 'k'; hostname[7] = '\0';
    }
    prompt("Enable /proc compatibility? [yes/no]: ", proc_compat, sizeof(proc_compat));
    if (is_yes(proc_compat)) {
        proc_enabled = 1;
    }

    printf("\nInstallation plan:\n");
    printf("  disk:      %s\n", target[0] ? target : "(unspecified)");
    printf("  root:      %s\n", target_root);
    printf("  hostname:  %s\n", hostname);
    printf("  /proc:     %s\n", proc_enabled ? "yes" : "no");

    prompt("Confirm and stage install tree now? [yes/no]: ", confirm, sizeof(confirm));
    if (!is_yes(confirm)) {
        printf("Aborted by user.\n");
        _exit(1);
    }

    if (perform_staging_install(target_root, target, hostname, proc_enabled) < 0) {
        printf("Failed to stage install tree.\n");
        _exit(1);
    }

    printf("\nInstall staging complete.\n");
    printf("  - live plan:   /etc/obelisk-install.conf\n");
    printf("  - target root: %s\n", target_root);
    printf("Next step: mount real target FS there, then rerun installer, then install bootloader.\n");

    _exit(0);
}
