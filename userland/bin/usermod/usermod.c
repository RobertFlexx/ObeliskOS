/*
 * Obelisk — usermod (minimal)
 *
 * Usage: usermod [-l name] [-d home] [-s shell] [-u uid] [-g gid|group] LOGIN
 * Root only. Rewrites /etc/passwd and /etc/group atomically.
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

static int parse_u32(const char *s, unsigned *out) {
    unsigned v = 0;
    if (!s || !s[0]) {
        return -1;
    }
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        v = v * 10u + (unsigned)(*s - '0');
        if (v > 2000000000u) {
            return -1;
        }
    }
    *out = v;
    return 0;
}

static int group_has_user(const char *mem, const char *user) {
    const char *p = mem;
    size_t ul = strlen(user);

    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);

        if (len == ul && strncmp(p, user, ul) == 0) {
            return 1;
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    return 0;
}

static void replace_member_name(ob_grdb_t *gr, const char *oldn, const char *newn) {
    int i;

    for (i = 0; i < gr->n; i++) {
        if (group_has_user(gr->ent[i].members, oldn)) {
            (void)ob_grdb_remove_member(gr, i, oldn);
            if (newn && newn[0]) {
                (void)ob_grdb_add_member(gr, i, newn);
            }
        }
    }
}

int main(int argc, char **argv) {
    int i;
    const char *newname = NULL;
    const char *home_opt = NULL;
    const char *shell_opt = NULL;
    int have_uid = 0;
    unsigned uid_opt = 0;
    int have_gid = 0;
    unsigned gid_opt = 0;
    const char *gname_opt = NULL;
    const char *login = NULL;
    ob_pwdb_t pw;
    ob_grdb_t gr;
    int pi;
    int gi;

    if (argc < 2) {
        wrerr("usage: usermod [-l name] [-d home] [-s shell] [-u uid] [-g gid|group] LOGIN\n");
        return 1;
    }
    if (acct_require_root() < 0) {
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            if (login) {
                wrerr("usermod: extra operand\n");
                return 1;
            }
            login = argv[i];
            continue;
        }
        if (strcmp(argv[i], "-l") == 0) {
            if (i + 1 >= argc) {
                wrerr("usermod: -l requires an argument\n");
                return 1;
            }
            newname = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-d") == 0) {
            if (i + 1 >= argc) {
                wrerr("usermod: -d requires an argument\n");
                return 1;
            }
            home_opt = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                wrerr("usermod: -s requires an argument\n");
                return 1;
            }
            shell_opt = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-u") == 0) {
            if (i + 1 >= argc || parse_u32(argv[i + 1], &uid_opt) < 0) {
                wrerr("usermod: bad -u\n");
                return 1;
            }
            have_uid = 1;
            i++;
            continue;
        }
        if (strcmp(argv[i], "-g") == 0) {
            if (i + 1 >= argc) {
                wrerr("usermod: -g requires an argument\n");
                return 1;
            }
            i++;
            if (parse_u32(argv[i], &gid_opt) == 0) {
                have_gid = 1;
            } else {
                gname_opt = argv[i];
            }
            continue;
        }
        wrerr("usermod: unknown option\n");
        return 1;
    }
    if (!login) {
        wrerr("usermod: missing LOGIN\n");
        return 1;
    }
    if (ob_pwdb_load(&pw, ACCT_PATH_PASSWD) < 0) {
        wrerr("usermod: cannot read /etc/passwd\n");
        return 1;
    }
    if (ob_grdb_load(&gr, ACCT_PATH_GROUP) < 0) {
        wrerr("usermod: cannot read /etc/group\n");
        return 1;
    }
    pi = ob_pwdb_find_name(&pw, login);
    if (pi < 0) {
        wrerr("usermod: user not found\n");
        return 1;
    }
    if (newname) {
        if (!ob_name_valid(newname)) {
            wrerr("usermod: invalid new username\n");
            return 1;
        }
        if (ob_pwdb_find_name(&pw, newname) >= 0) {
            wrerr("usermod: new username already exists\n");
            return 1;
        }
        replace_member_name(&gr, login, newname);
        strncpy(pw.ent[pi].name, newname, sizeof(pw.ent[pi].name) - 1);
        pw.ent[pi].name[sizeof(pw.ent[pi].name) - 1] = '\0';
        login = newname;
    }
    if (home_opt) {
        strncpy(pw.ent[pi].home, home_opt, sizeof(pw.ent[pi].home) - 1);
        pw.ent[pi].home[sizeof(pw.ent[pi].home) - 1] = '\0';
    }
    if (shell_opt) {
        strncpy(pw.ent[pi].shell, shell_opt, sizeof(pw.ent[pi].shell) - 1);
        pw.ent[pi].shell[sizeof(pw.ent[pi].shell) - 1] = '\0';
    }
    if (have_uid) {
        int uix = ob_pwdb_find_uid(&pw, uid_opt);

        if (uix >= 0 && strcmp(pw.ent[uix].name, login) != 0) {
            wrerr("usermod: uid already in use\n");
            return 1;
        }
        pw.ent[pi].uid = uid_opt;
    }
    if (have_gid && gname_opt == NULL) {
        gi = ob_grdb_find_gid(&gr, gid_opt);
        if (gi < 0) {
            wrerr("usermod: group not found for -g\n");
            return 1;
        }
        pw.ent[pi].gid = gid_opt;
    } else if (gname_opt) {
        gi = ob_grdb_find_name(&gr, gname_opt);
        if (gi < 0) {
            wrerr("usermod: group not found\n");
            return 1;
        }
        pw.ent[pi].gid = gr.ent[gi].gid;
    }

    if (ob_pwdb_save(&pw, ACCT_PATH_PASSWD) < 0) {
        wrerr("usermod: failed to write /etc/passwd\n");
        return 1;
    }
    if (ob_grdb_save(&gr, ACCT_PATH_GROUP) < 0) {
        wrerr("usermod: failed to write /etc/group\n");
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
