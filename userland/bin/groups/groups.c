/*
 * Obelisk — groups: print group memberships for current (or named) user.
 */

#include "../../lib/passwd_db.h"

#include <string.h>

typedef long ssize_t;

extern int getuid(void);
extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);

static void wrout(const char *s) {
    (void)write(1, s, strlen(s));
}

static int find_uid(const ob_pwdb_t *pw, const char *name, unsigned *uid_out) {
    int i;

    for (i = 0; i < pw->n; i++) {
        if (strcmp(pw->ent[i].name, name) == 0) {
            *uid_out = pw->ent[i].uid;
            return 0;
        }
    }
    return -1;
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

static int in_members(const char *mem, const char *user) {
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

static void print_u32(unsigned v) {
    char buf[16];
    size_t i = 0;
    size_t j;

    if (v == 0) {
        wrout("0");
        return;
    }
    while (v > 0 && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    j = i;
    while (j > 0) {
        char c = buf[--j];
        char t[2] = { c, '\0' };
        wrout(t);
    }
}

int main(int argc, char **argv) {
    ob_pwdb_t pw;
    ob_grdb_t gr;
    char uname[OB_PW_NAME_MAX];
    unsigned uid;
    unsigned gid = 0;
    int i;
    int first = 1;

    if (argc > 2) {
        wrout("usage: groups [user]\n");
        return 1;
    }
    ob_pwdb_load_or_empty(&pw, "/etc/passwd");
    ob_grdb_load_or_empty(&gr, "/etc/group");
    if (argc == 2) {
        if (find_uid(&pw, argv[1], &uid) < 0) {
            wrout("groups: unknown user\n");
            return 1;
        }
        strncpy(uname, argv[1], sizeof(uname) - 1);
        uname[sizeof(uname) - 1] = '\0';
    } else {
        uid = (unsigned)getuid();
        if (find_user_by_uid(&pw, uid, uname, sizeof(uname)) < 0) {
            wrout("groups: unknown uid\n");
            return 1;
        }
    }
    for (i = 0; i < pw.n; i++) {
        if (pw.ent[i].uid == uid) {
            gid = pw.ent[i].gid;
            break;
        }
    }
    for (i = 0; i < gr.n; i++) {
        if (gr.ent[i].gid == gid) {
            wrout(gr.ent[i].name);
            first = 0;
            break;
        }
    }
    if (first) {
        print_u32(gid);
        first = 0;
    }
    for (i = 0; i < gr.n; i++) {
        if (gr.ent[i].gid == gid) {
            continue;
        }
        if (in_members(gr.ent[i].members, uname)) {
            wrout(" ");
            wrout(gr.ent[i].name);
        }
    }
    wrout("\n");
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
