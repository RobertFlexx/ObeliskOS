/*
 * Obelisk — passwd (password change)
 *
 * Usage: passwd [user]
 * Root may set any user's password. Non-root users may only change their own password
 * and must enter the current password first.
 * Passwords are stored as fnv1a64$hex (same as rockbox).
 */

#include "../../lib/passwd_db.h"

#include <string.h>

typedef long ssize_t;

#define PASSWD_PATH "/etc/passwd"

extern int geteuid(void);
extern int getuid(void);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);

static void wrerr(const char *s) {
    (void)write(2, s, strlen(s));
}

static int prompt_password(const char *prompt, char *buf, size_t cap) {
    ssize_t n;
    size_t len;

    if (cap < 2) {
        return -1;
    }
    (void)write(1, prompt, strlen(prompt));
    n = read(0, buf, cap - 1);
    if (n < 0) {
        return -1;
    }
    len = (size_t)n;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        len--;
    }
    buf[len] = '\0';
    return 0;
}

static int find_user_by_uid(const ob_pwdb_t *pw, unsigned uid, char *name, size_t cap) {
    int i;

    for (i = 0; i < pw->n; i++) {
        if (pw->ent[i].uid == uid) {
            strncpy(name, pw->ent[i].name, cap - 1);
            name[cap - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    ob_pwdb_t pw;
    int pi;
    int isroot = (geteuid() == 0);
    unsigned ruid = (unsigned)getuid();
    char target[OB_PW_NAME_MAX];
    char newpass[128];
    char newpass2[128];
    char oldpass[128];
    char hash[OB_PW_PASS_MAX];

    if (argc > 2) {
        wrerr("usage: passwd [user]\n");
        return 1;
    }
    if (ob_pwdb_load(&pw, PASSWD_PATH) < 0) {
        wrerr("passwd: cannot read /etc/passwd\n");
        return 1;
    }
    if (argc == 2) {
        if (!isroot) {
            wrerr("passwd: only root may change another user's password\n");
            return 1;
        }
        strncpy(target, argv[1], sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
    } else {
        if (find_user_by_uid(&pw, ruid, target, sizeof(target)) < 0) {
            wrerr("passwd: unknown uid for current user\n");
            return 1;
        }
    }
    pi = ob_pwdb_find_name(&pw, target);
    if (pi < 0) {
        wrerr("passwd: unknown user\n");
        return 1;
    }
    if (!isroot) {
        if (prompt_password("Current password: ", oldpass, sizeof(oldpass)) < 0) {
            wrerr("passwd: read error\n");
            return 1;
        }
        if (!ob_verify_password(pw.ent[pi].pass, oldpass)) {
            wrerr("passwd: authentication failure\n");
            return 1;
        }
    }
    if (prompt_password("New password: ", newpass, sizeof(newpass)) < 0) {
        wrerr("passwd: read error\n");
        return 1;
    }
    if (prompt_password("Retype new password: ", newpass2, sizeof(newpass2)) < 0) {
        wrerr("passwd: read error\n");
        return 1;
    }
    if (strcmp(newpass, newpass2) != 0) {
        wrerr("passwd: passwords do not match\n");
        return 1;
    }
    if (newpass[0] == '\0') {
        wrerr("passwd: empty password not allowed\n");
        return 1;
    }
    ob_hash_password(newpass, hash, sizeof(hash));
    strncpy(pw.ent[pi].pass, hash, sizeof(pw.ent[pi].pass) - 1);
    pw.ent[pi].pass[sizeof(pw.ent[pi].pass) - 1] = '\0';
    if (ob_pwdb_save(&pw, PASSWD_PATH) < 0) {
        wrerr("passwd: failed to write /etc/passwd\n");
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
