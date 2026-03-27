/*
 * Obelisk — /etc/passwd and /etc/group helpers (small, auditable).
 */

#include "passwd_db.h"

#include <string.h>

typedef long ssize_t;

extern int open(const char *pathname, int flags, int mode);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int close(int fd);
extern int unlink(const char *pathname);
extern int rename(const char *oldpath, const char *newpath);

static void u64_to_hex16(uint64_t v, char *out) {
    static const char *xd = "0123456789abcdef";
    int i;
    for (i = 15; i >= 0; i--) {
        out[i] = xd[v & 0xf];
        v >>= 4;
    }
    out[16] = '\0';
}

uint64_t ob_fnv1a64(const char *s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; s[i]; i++) {
        h ^= (uint8_t)s[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

void ob_hash_password(const char *plain, char *out, size_t cap) {
    uint64_t h = ob_fnv1a64(plain);
    if (cap < 32) {
        if (cap > 0) out[0] = '\0';
        return;
    }
    memcpy(out, "fnv1a64$", 8);
    u64_to_hex16(h, out + 8);
}

static int parse_hex_u64(const char *s, uint64_t *out) {
    uint64_t v = 0;
    int n = 0;
    while (s[n]) {
        char c = s[n];
        uint64_t d;
        if (c >= '0' && c <= '9') {
            d = (uint64_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (uint64_t)(10 + c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            d = (uint64_t)(10 + c - 'A');
        } else {
            return -1;
        }
        v = (v << 4) | d;
        n++;
    }
    if (n == 0 || n > 16) {
        return -1;
    }
    *out = v;
    return 0;
}

int ob_verify_password(const char *stored, const char *input) {
    if (!stored || stored[0] == '\0') {
        return 1;
    }
    if (stored[0] == 'x' && stored[1] == '\0') {
        return 0;
    }
    if (stored[0] == '*' && stored[1] == '\0') {
        return 0;
    }
    if (strncmp(stored, "plain$", 6) == 0) {
        return strcmp(stored + 6, input) == 0;
    }
    if (strncmp(stored, "fnv1a64$", 8) == 0) {
        uint64_t hv = 0;
        if (parse_hex_u64(stored + 8, &hv) < 0) {
            return 0;
        }
        return ob_fnv1a64(input) == hv;
    }
    return strcmp(stored, input) == 0;
}

int ob_name_valid(const char *name) {
    size_t i, n;
    if (!name || !name[0]) {
        return 0;
    }
    n = strlen(name);
    if (n >= OB_PW_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = name[i];
        if (i == 0) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                return 0;
            }
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
                return 0;
            }
        }
    }
    return 1;
}

int ob_read_file(const char *path, char *buf, size_t cap, size_t *out_len) {
    int fd = open(path, 0, 0);
    long n;
    size_t total = 0;
    if (fd < 0) {
        return -1;
    }
    while (total + 1 < cap) {
        n = read(fd, buf + total, cap - total - 1);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    close(fd);
    buf[total] = '\0';
    if (out_len) {
        *out_len = total;
    }
    return 0;
}

int ob_write_atomic(const char *path, const char *data, size_t len) {
    char tmp[512];
    int fd;
    long w;
    size_t pos = 0;
    size_t pl = strlen(path);
    if (pl + 10 >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, pl);
    memcpy(tmp + pl, ".new", 5);
    fd = open(tmp, 0x241, 0644);
    if (fd < 0) {
        return -1;
    }
    while (pos < len) {
        w = write(fd, data + pos, len - pos);
        if (w < 0) {
            close(fd);
            unlink(tmp);
            return -1;
        }
        pos += (size_t)w;
    }
    close(fd);
    if (rename(tmp, path) < 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static char *split_col(char *line, int which) {
    char *p = line;
    int i = 0;
    while (i < which) {
        while (*p && *p != ':') {
            p++;
        }
        if (*p != ':') {
            return NULL;
        }
        p++;
        i++;
    }
    return p;
}

static int count_field(const char *s) {
    int n = 0;
    while (*s && *s != ':' && *s != '\n' && *s != '\r') {
        n++;
        s++;
    }
    return n;
}

int ob_passwd_parse_line(char *line, ob_pw_entry_t *out) {
    char *f0, *f1, *f2, *f3, *f4, *f5, *f6;
    unsigned uid = 0, gid = 0;
    size_t i;

    if (!line || !out) {
        return -1;
    }
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (line[0] == '#' || line[0] == '\0') {
        return -1;
    }
    f0 = line;
    f1 = split_col(line, 1);
    f2 = split_col(line, 2);
    f3 = split_col(line, 3);
    f4 = split_col(line, 4);
    f5 = split_col(line, 5);
    f6 = split_col(line, 6);
    if (!f1 || !f2 || !f3 || !f4 || !f5 || !f6) {
        return -1;
    }
    f1[-1] = '\0';
    f2[-1] = '\0';
    f3[-1] = '\0';
    f4[-1] = '\0';
    f5[-1] = '\0';
    f6[-1] = '\0';
    for (i = 0; f0[i] && i < sizeof(out->name) - 1; i++) {
        out->name[i] = f0[i];
    }
    out->name[i] = '\0';
    for (i = 0; f1[i] && i < sizeof(out->pass) - 1; i++) {
        out->pass[i] = f1[i];
    }
    out->pass[i] = '\0';
    if (!f2[0] || !f3[0]) {
        return -1;
    }
    uid = 0;
    for (i = 0; f2[i]; i++) {
        if (f2[i] < '0' || f2[i] > '9') {
            return -1;
        }
        uid = uid * 10u + (unsigned)(f2[i] - '0');
        if (uid > 2000000000u) {
            return -1;
        }
    }
    gid = 0;
    for (i = 0; f3[i]; i++) {
        if (f3[i] < '0' || f3[i] > '9') {
            return -1;
        }
        gid = gid * 10u + (unsigned)(f3[i] - '0');
        if (gid > 2000000000u) {
            return -1;
        }
    }
    out->uid = uid;
    out->gid = gid;
    for (i = 0; f4[i] && i < sizeof(out->gecos) - 1; i++) {
        out->gecos[i] = f4[i];
    }
    out->gecos[i] = '\0';
    for (i = 0; f5[i] && i < sizeof(out->home) - 1; i++) {
        out->home[i] = f5[i];
    }
    out->home[i] = '\0';
    for (i = 0; f6[i] && f6[i] != '\n' && f6[i] != '\r' && i < sizeof(out->shell) - 1; i++) {
        out->shell[i] = f6[i];
    }
    out->shell[i] = '\0';
    for (i = 0; out->name[i]; i++) {
        if (out->name[i] == ':' || out->name[i] == '\n') {
            return -1;
        }
    }
    (void)count_field;
    return 0;
}

static void fmt_dec(unsigned v, char *buf, size_t cap) {
    char tmp[16];
    size_t i = 0;
    size_t j;
    if (cap < 2) {
        if (cap) {
            buf[0] = '\0';
        }
        return;
    }
    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (v > 0 && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    j = 0;
    while (i > 0 && j + 1 < cap) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

int ob_passwd_format_line(const ob_pw_entry_t *e, char *out, size_t cap) {
    char ubuf[16], gbuf[16];
    const char *p;
    int n = 0;

    fmt_dec(e->uid, ubuf, sizeof(ubuf));
    fmt_dec(e->gid, gbuf, sizeof(gbuf));
    p = e->name;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = ':';
    p = e->pass;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = ':';
    p = ubuf;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = ':';
    p = gbuf;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = ':';
    p = e->gecos;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = ':';
    p = e->home;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = ':';
    p = e->shell;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = '\n';
    out[n] = '\0';
    return n;
}

int ob_group_parse_line(char *line, ob_gr_entry_t *out) {
    char *f0, *f1, *f2, *f3;
    unsigned gid = 0;
    size_t i;

    if (!line || !out) {
        return -1;
    }
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (line[0] == '#' || line[0] == '\0') {
        return -1;
    }
    f0 = line;
    f1 = split_col(line, 1);
    f2 = split_col(line, 2);
    f3 = split_col(line, 3);
    if (!f1 || !f2 || !f3) {
        return -1;
    }
    f1[-1] = '\0';
    f2[-1] = '\0';
    f3[-1] = '\0';
    for (i = 0; f0[i] && i < sizeof(out->name) - 1; i++) {
        out->name[i] = f0[i];
    }
    out->name[i] = '\0';
    out->pass[0] = '\0';
    if (f1[0]) {
        out->pass[0] = f1[0];
        out->pass[1] = '\0';
    }
    gid = 0;
    for (i = 0; f2[i]; i++) {
        if (f2[i] < '0' || f2[i] > '9') {
            return -1;
        }
        gid = gid * 10u + (unsigned)(f2[i] - '0');
    }
    out->gid = gid;
    for (i = 0; f3[i] && f3[i] != '\n' && f3[i] != '\r' && i < sizeof(out->members) - 1; i++) {
        out->members[i] = f3[i];
    }
    out->members[i] = '\0';
    return 0;
}

int ob_group_format_line(const ob_gr_entry_t *e, char *out, size_t cap) {
    char gbuf[16];
    int n = 0;
    const char *p;

    fmt_dec(e->gid, gbuf, sizeof(gbuf));
    p = e->name;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = ':';
    out[n++] = 'x';
    out[n++] = ':';
    p = gbuf;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = ':';
    p = e->members;
    while (*p && n < (int)cap - 1) {
        out[n++] = *p++;
    }
    out[n++] = '\n';
    out[n] = '\0';
    return n;
}

void ob_pwdb_init(ob_pwdb_t *db) {
    db->n = 0;
}

static int pwdb_parse_buffer(ob_pwdb_t *db, char *buf) {
    char *line, *next;

    line = buf;
    while (*line) {
        while (*line == '\n' || *line == '\r') {
            line++;
        }
        if (!*line) {
            break;
        }
        next = line;
        while (*next && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next++ = '\0';
        }
        if (line[0] && line[0] != '#') {
            ob_pw_entry_t e;
            memset(&e, 0, sizeof(e));
            if (ob_passwd_parse_line(line, &e) == 0 && db->n < OB_PW_MAX_LINES) {
                db->ent[db->n++] = e;
            }
        }
        line = next;
    }
    return 0;
}

int ob_pwdb_load(ob_pwdb_t *db, const char *path) {
    char buf[OB_PW_FILE_MAX];
    size_t len;

    ob_pwdb_init(db);
    if (ob_read_file(path, buf, sizeof(buf), &len) < 0) {
        return -1;
    }
    return pwdb_parse_buffer(db, buf);
}

int ob_pwdb_load_or_empty(ob_pwdb_t *db, const char *path) {
    char buf[OB_PW_FILE_MAX];
    size_t len;

    ob_pwdb_init(db);
    if (ob_read_file(path, buf, sizeof(buf), &len) < 0) {
        return 0;
    }
    return pwdb_parse_buffer(db, buf);
}

static int build_pwbuf(const ob_pwdb_t *db, char *buf, size_t cap) {
    int pos = 0;
    int i, n;
    for (i = 0; i < db->n; i++) {
        n = ob_passwd_format_line(&db->ent[i], buf + pos, cap - (size_t)pos);
        if (n < 0 || pos + n >= (int)cap) {
            return -1;
        }
        pos += n;
    }
    buf[pos] = '\0';
    return pos;
}

int ob_pwdb_save(const ob_pwdb_t *db, const char *path) {
    char buf[OB_PW_FILE_MAX];
    int n = build_pwbuf(db, buf, sizeof(buf));
    if (n < 0) {
        return -1;
    }
    return ob_write_atomic(path, buf, (size_t)n);
}

int ob_pwdb_find_name(const ob_pwdb_t *db, const char *name) {
    int i;
    for (i = 0; i < db->n; i++) {
        if (strcmp(db->ent[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int ob_pwdb_find_uid(const ob_pwdb_t *db, unsigned uid) {
    int i;
    for (i = 0; i < db->n; i++) {
        if (db->ent[i].uid == uid) {
            return i;
        }
    }
    return -1;
}

int ob_pwdb_append(ob_pwdb_t *db, const ob_pw_entry_t *e) {
    if (db->n >= OB_PW_MAX_LINES) {
        return -1;
    }
    db->ent[db->n++] = *e;
    return 0;
}

void ob_pwdb_remove_at(ob_pwdb_t *db, int idx) {
    int i;
    if (idx < 0 || idx >= db->n) {
        return;
    }
    for (i = idx + 1; i < db->n; i++) {
        db->ent[i - 1] = db->ent[i];
    }
    db->n--;
}

int ob_pwdb_next_uid(const ob_pwdb_t *db, unsigned min_uid, unsigned max_uid) {
    unsigned u;
    for (u = min_uid; u <= max_uid; u++) {
        if (ob_pwdb_find_uid(db, u) < 0) {
            return (int)u;
        }
    }
    return -1;
}

void ob_grdb_init(ob_grdb_t *db) {
    db->n = 0;
}

static int grdb_parse_buffer(ob_grdb_t *db, char *buf) {
    char *line, *next;

    line = buf;
    while (*line) {
        while (*line == '\n' || *line == '\r') {
            line++;
        }
        if (!*line) {
            break;
        }
        next = line;
        while (*next && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next++ = '\0';
        }
        if (line[0] && line[0] != '#') {
            ob_gr_entry_t e;
            memset(&e, 0, sizeof(e));
            if (ob_group_parse_line(line, &e) == 0 && db->n < OB_GR_MAX_LINES) {
                db->ent[db->n++] = e;
            }
        }
        line = next;
    }
    return 0;
}

int ob_grdb_load(ob_grdb_t *db, const char *path) {
    char buf[OB_PW_FILE_MAX];
    size_t len;

    ob_grdb_init(db);
    if (ob_read_file(path, buf, sizeof(buf), &len) < 0) {
        return -1;
    }
    return grdb_parse_buffer(db, buf);
}

int ob_grdb_load_or_empty(ob_grdb_t *db, const char *path) {
    char buf[OB_PW_FILE_MAX];
    size_t len;

    ob_grdb_init(db);
    if (ob_read_file(path, buf, sizeof(buf), &len) < 0) {
        return 0;
    }
    return grdb_parse_buffer(db, buf);
}

static int build_grbuf(const ob_grdb_t *db, char *buf, size_t cap) {
    int pos = 0;
    int i, n;
    for (i = 0; i < db->n; i++) {
        n = ob_group_format_line(&db->ent[i], buf + pos, cap - (size_t)pos);
        if (n < 0 || pos + n >= (int)cap) {
            return -1;
        }
        pos += n;
    }
    buf[pos] = '\0';
    return pos;
}

int ob_grdb_save(const ob_grdb_t *db, const char *path) {
    char buf[OB_PW_FILE_MAX];
    int n = build_grbuf(db, buf, sizeof(buf));
    if (n < 0) {
        return -1;
    }
    return ob_write_atomic(path, buf, (size_t)n);
}

int ob_grdb_find_name(const ob_grdb_t *db, const char *name) {
    int i;
    for (i = 0; i < db->n; i++) {
        if (strcmp(db->ent[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int ob_grdb_find_gid(const ob_grdb_t *db, unsigned gid) {
    int i;
    for (i = 0; i < db->n; i++) {
        if (db->ent[i].gid == gid) {
            return i;
        }
    }
    return -1;
}

int ob_grdb_append(ob_grdb_t *db, const ob_gr_entry_t *e) {
    if (db->n >= OB_GR_MAX_LINES) {
        return -1;
    }
    db->ent[db->n++] = *e;
    return 0;
}

void ob_grdb_remove_at(ob_grdb_t *db, int idx) {
    int i;
    if (idx < 0 || idx >= db->n) {
        return;
    }
    for (i = idx + 1; i < db->n; i++) {
        db->ent[i - 1] = db->ent[i];
    }
    db->n--;
}

int ob_grdb_next_gid(const ob_grdb_t *db, unsigned min_gid, unsigned max_gid) {
    unsigned g;
    for (g = min_gid; g <= max_gid; g++) {
        if (ob_grdb_find_gid(db, g) < 0) {
            return (int)g;
        }
    }
    return -1;
}

static int mem_has_token(const char *mem, const char *user) {
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

int ob_grdb_add_member(ob_grdb_t *db, int idx, const char *user) {
    ob_gr_entry_t *e;
    size_t len;
    if (idx < 0 || idx >= db->n) {
        return -1;
    }
    e = &db->ent[idx];
    if (mem_has_token(e->members, user)) {
        return 0;
    }
    len = strlen(e->members);
    if (len > 0) {
        if (len + 2 >= sizeof(e->members)) {
            return -1;
        }
        e->members[len++] = ',';
    }
    strncpy(e->members + len, user, sizeof(e->members) - len - 1);
    e->members[sizeof(e->members) - 1] = '\0';
    return 0;
}

int ob_grdb_remove_member(ob_grdb_t *db, int idx, const char *user) {
    ob_gr_entry_t *e;
    char out[OB_GR_MEMB_MAX];
    const char *p;
    int first = 1;
    size_t o = 0;
    size_t ul = strlen(user);

    if (idx < 0 || idx >= db->n) {
        return -1;
    }
    e = &db->ent[idx];
    p = e->members;
    out[0] = '\0';
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (!(len == ul && strncmp(p, user, ul) == 0)) {
            if (!first && o + 1 < sizeof(out)) {
                out[o++] = ',';
            }
            first = 0;
            if (o + len < sizeof(out)) {
                memcpy(out + o, p, len);
                o += len;
                out[o] = '\0';
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    strncpy(e->members, out, sizeof(e->members) - 1);
    e->members[sizeof(e->members) - 1] = '\0';
    return 0;
}
