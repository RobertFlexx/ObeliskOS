/*
 * Obelisk — useradd (minimal)
 *
 * Usage: useradd [-m] [-d home] [-s shell] [-u uid] [-g gid|group] LOGIN
 * Root only. Creates /etc/passwd and /etc/group entries; default password field is '*' (locked).
 * With -m, creates HOME and chowns to the new uid:gid.
 */

#include "../../lib/passwd_db.h"
#include "../../lib/account_common.h"

#include <string.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern int chown(const char *pathname, int owner, int group);
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

int main(int argc, char **argv) {
    int i;
    int m_home = 0;
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
    ob_pw_entry_t ne;
    unsigned uid;
    unsigned gid;
    int gi;
    ob_gr_entry_t ge;

    if (argc < 2) {
        wrerr("usage: useradd [-m] [-d home] [-s shell] [-u uid] [-g gid|group] LOGIN\n");
        return 1;
    }
    if (acct_require_root() < 0) {
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            if (login) {
                wrerr("useradd: extra operand\n");
                return 1;
            }
            login = argv[i];
            continue;
        }
        if (strcmp(argv[i], "-m") == 0) {
            m_home = 1;
            continue;
        }
        if (strcmp(argv[i], "-d") == 0) {
            if (i + 1 >= argc) {
                wrerr("useradd: -d requires an argument\n");
                return 1;
            }
            home_opt = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                wrerr("useradd: -s requires an argument\n");
                return 1;
            }
            shell_opt = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-u") == 0) {
            if (i + 1 >= argc || parse_u32(argv[i + 1], &uid_opt) < 0) {
                wrerr("useradd: bad -u\n");
                return 1;
            }
            have_uid = 1;
            i++;
            continue;
        }
        if (strcmp(argv[i], "-g") == 0) {
            if (i + 1 >= argc) {
                wrerr("useradd: -g requires an argument\n");
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
        wrerr("useradd: unknown option\n");
        return 1;
    }
    if (!login) {
        wrerr("useradd: missing LOGIN\n");
        return 1;
    }
    if (!ob_name_valid(login)) {
        wrerr("useradd: invalid username\n");
        return 1;
    }
    ob_pwdb_load_or_empty(&pw, ACCT_PATH_PASSWD);
    ob_grdb_load_or_empty(&gr, ACCT_PATH_GROUP);
    if (ob_pwdb_find_name(&pw, login) >= 0) {
        wrerr("useradd: user already exists\n");
        return 1;
    }
    if (have_uid) {
        if (ob_pwdb_find_uid(&pw, uid_opt) >= 0) {
            wrerr("useradd: uid already in use\n");
            return 1;
        }
        uid = uid_opt;
    } else {
        int nu = ob_pwdb_next_uid(&pw, ACCT_MIN_UID, ACCT_MAX_UID);
        if (nu < 0) {
            wrerr("useradd: no free uid\n");
            return 1;
        }
        uid = (unsigned)nu;
    }
    if (have_gid && gname_opt == NULL) {
        gi = ob_grdb_find_gid(&gr, gid_opt);
        if (gi < 0) {
            wrerr("useradd: group not found for -g\n");
            return 1;
        }
        gid = gid_opt;
    } else if (gname_opt) {
        gi = ob_grdb_find_name(&gr, gname_opt);
        if (gi < 0) {
            wrerr("useradd: group not found\n");
            return 1;
        }
        gid = gr.ent[gi].gid;
    } else {
        if (ob_grdb_find_name(&gr, login) >= 0) {
            wrerr("useradd: group name exists; use -g\n");
            return 1;
        }
        if (ob_grdb_find_gid(&gr, uid) >= 0) {
            int ng = ob_grdb_next_gid(&gr, ACCT_MIN_GID, ACCT_MAX_GID);
            if (ng < 0) {
                wrerr("useradd: no free gid\n");
                return 1;
            }
            gid = (unsigned)ng;
        } else {
            gid = uid;
        }
    }

    memset(&ne, 0, sizeof(ne));
    strncpy(ne.name, login, sizeof(ne.name) - 1);
    ne.name[sizeof(ne.name) - 1] = '\0';
    strncpy(ne.pass, "*", sizeof(ne.pass) - 1);
    ne.pass[sizeof(ne.pass) - 1] = '\0';
    ne.uid = uid;
    ne.gid = gid;
    strncpy(ne.gecos, login, sizeof(ne.gecos) - 1);
    if (home_opt) {
        strncpy(ne.home, home_opt, sizeof(ne.home) - 1);
    } else {
        size_t k = 0;
        const char *p = "/home/";
        while (*p && k + 1 < sizeof(ne.home)) {
            ne.home[k++] = *p++;
        }
        p = login;
        while (*p && k + 1 < sizeof(ne.home)) {
            ne.home[k++] = *p++;
        }
        ne.home[k] = '\0';
    }
    ne.home[sizeof(ne.home) - 1] = '\0';
    if (shell_opt) {
        strncpy(ne.shell, shell_opt, sizeof(ne.shell) - 1);
    } else {
        strncpy(ne.shell, ACCT_DEF_SHELL, sizeof(ne.shell) - 1);
    }
    ne.shell[sizeof(ne.shell) - 1] = '\0';

    if (!have_gid && !gname_opt) {
        memset(&ge, 0, sizeof(ge));
        strncpy(ge.name, login, sizeof(ge.name) - 1);
        ge.pass[0] = 'x';
        ge.pass[1] = '\0';
        ge.gid = gid;
        strncpy(ge.members, login, sizeof(ge.members) - 1);
        ge.members[sizeof(ge.members) - 1] = '\0';
        if (ob_grdb_append(&gr, &ge) < 0) {
            wrerr("useradd: group table full\n");
            return 1;
        }
    }

    if (ob_pwdb_append(&pw, &ne) < 0) {
        wrerr("useradd: passwd table full\n");
        return 1;
    }
    gi = ob_grdb_find_gid(&gr, gid);
    if (gi >= 0 && ob_grdb_add_member(&gr, gi, login) < 0) {
        wrerr("useradd: could not add user to group members list\n");
        return 1;
    }
    if (ob_pwdb_save(&pw, ACCT_PATH_PASSWD) < 0) {
        wrerr("useradd: failed to write /etc/passwd\n");
        return 1;
    }
    if (ob_grdb_save(&gr, ACCT_PATH_GROUP) < 0) {
        wrerr("useradd: failed to write /etc/group\n");
        return 1;
    }

    if (m_home) {
        if (acct_mkdir_p(ne.home, 0755) < 0) {
            wrerr("useradd: could not create home directory\n");
            return 1;
        }
        if (chown(ne.home, (int)uid, (int)gid) < 0) {
            wrerr("useradd: chown home failed\n");
            return 1;
        }
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
