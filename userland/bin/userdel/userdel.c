/*
 * Obelisk — userdel (minimal)
 *
 * Usage: userdel [-r] LOGIN
 * Root only. Removes the passwd entry and removes LOGIN from all group member lists.
 * If a group exists with the same name as LOGIN (primary group line), that group is removed.
 *
 * -r: recursively remove the user's home directory (best-effort).
 */

#include "../../lib/passwd_db.h"
#include "../../lib/account_common.h"

#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);

static void wrerr(const char *s) {
    (void)write(2, s, strlen(s));
}

int main(int argc, char **argv) {
    int i;
    int rmhome = 0;
    const char *login = NULL;
    ob_pwdb_t pw;
    ob_grdb_t gr;
    int pi;
    int gi;
    ob_pw_entry_t copy;
    int g;

    if (argc < 2) {
        wrerr("usage: userdel [-r] LOGIN\n");
        return 1;
    }
    if (acct_require_root() < 0) {
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            rmhome = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            wrerr("userdel: unknown option\n");
            return 1;
        }
        if (login) {
            wrerr("userdel: extra operand\n");
            return 1;
        }
        login = argv[i];
    }
    if (!login) {
        wrerr("userdel: missing LOGIN\n");
        return 1;
    }
    if (ob_pwdb_load_or_empty(&pw, ACCT_PATH_PASSWD) < 0) {
        wrerr("userdel: cannot read /etc/passwd\n");
        return 1;
    }
    if (ob_grdb_load_or_empty(&gr, ACCT_PATH_GROUP) < 0) {
        wrerr("userdel: cannot read /etc/group\n");
        return 1;
    }
    pi = ob_pwdb_find_name(&pw, login);
    if (pi < 0) {
        wrerr("userdel: user not found\n");
        return 1;
    }
    copy = pw.ent[pi];
    if (rmhome && copy.home[0]) {
        if (acct_rm_tree(copy.home) < 0) {
            wrerr("userdel: could not remove home (-r)\n");
            return 1;
        }
    }
    ob_pwdb_remove_at(&pw, pi);
    for (g = 0; g < gr.n; g++) {
        (void)ob_grdb_remove_member(&gr, g, login);
    }
    gi = ob_grdb_find_name(&gr, login);
    if (gi >= 0 && gr.ent[gi].gid == copy.gid) {
        ob_grdb_remove_at(&gr, gi);
    }
    if (ob_pwdb_save(&pw, ACCT_PATH_PASSWD) < 0) {
        wrerr("userdel: failed to write /etc/passwd\n");
        return 1;
    }
    if (ob_grdb_save(&gr, ACCT_PATH_GROUP) < 0) {
        wrerr("userdel: failed to write /etc/group\n");
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
