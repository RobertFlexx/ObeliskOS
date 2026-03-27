/*
 * Obelisk — minimal /etc/passwd and /etc/group database helpers.
 *
 * Supported /etc/passwd line (7 colon fields, no embedded newlines in fields):
 *   name:password:uid:gid:gecos:home:shell
 * password: empty, x, *, plain$..., or fnv1a64$hex (same as rockbox).
 *
 * Supported /etc/group line (4 fields):
 *   name:password:gid:member_list
 * member_list: comma-separated usernames, no spaces.
 */

#ifndef OB_PASSWD_DB_H
#define OB_PASSWD_DB_H

#include <stddef.h>
#include <stdint.h>

#define OB_PW_NAME_MAX 32
#define OB_PW_PASS_MAX 160
#define OB_PW_GECOS_MAX 64
#define OB_PW_HOME_MAX 128
#define OB_PW_SHELL_MAX 128
#define OB_PW_MAX_LINES 256
#define OB_PW_FILE_MAX 65536

#define OB_GR_NAME_MAX 32
#define OB_GR_MEMB_MAX 512
#define OB_GR_MAX_LINES 256

typedef struct ob_pw_entry {
    char name[OB_PW_NAME_MAX];
    char pass[OB_PW_PASS_MAX];
    unsigned uid;
    unsigned gid;
    char gecos[OB_PW_GECOS_MAX];
    char home[OB_PW_HOME_MAX];
    char shell[OB_PW_SHELL_MAX];
} ob_pw_entry_t;

typedef struct ob_gr_entry {
    char name[OB_GR_NAME_MAX];
    char pass[8];
    unsigned gid;
    char members[OB_GR_MEMB_MAX];
} ob_gr_entry_t;

typedef struct ob_pwdb {
    ob_pw_entry_t ent[OB_PW_MAX_LINES];
    int n;
} ob_pwdb_t;

typedef struct ob_grdb {
    ob_gr_entry_t ent[OB_GR_MAX_LINES];
    int n;
} ob_grdb_t;

int ob_read_file(const char *path, char *buf, size_t cap, size_t *out_len);
int ob_write_atomic(const char *path, const char *data, size_t len);

int ob_passwd_parse_line(char *line, ob_pw_entry_t *out);
int ob_passwd_format_line(const ob_pw_entry_t *e, char *out, size_t cap);

int ob_group_parse_line(char *line, ob_gr_entry_t *out);
int ob_group_format_line(const ob_gr_entry_t *e, char *out, size_t cap);

void ob_pwdb_init(ob_pwdb_t *db);
int ob_pwdb_load(ob_pwdb_t *db, const char *path);
int ob_pwdb_load_or_empty(ob_pwdb_t *db, const char *path);
int ob_pwdb_save(const ob_pwdb_t *db, const char *path);
int ob_pwdb_find_name(const ob_pwdb_t *db, const char *name);
int ob_pwdb_find_uid(const ob_pwdb_t *db, unsigned uid);
int ob_pwdb_append(ob_pwdb_t *db, const ob_pw_entry_t *e);
void ob_pwdb_remove_at(ob_pwdb_t *db, int idx);
int ob_pwdb_next_uid(const ob_pwdb_t *db, unsigned min_uid, unsigned max_uid);

void ob_grdb_init(ob_grdb_t *db);
int ob_grdb_load(ob_grdb_t *db, const char *path);
int ob_grdb_load_or_empty(ob_grdb_t *db, const char *path);
int ob_grdb_save(const ob_grdb_t *db, const char *path);
int ob_grdb_find_name(const ob_grdb_t *db, const char *name);
int ob_grdb_find_gid(const ob_grdb_t *db, unsigned gid);
int ob_grdb_append(ob_grdb_t *db, const ob_gr_entry_t *e);
void ob_grdb_remove_at(ob_grdb_t *db, int idx);
int ob_grdb_next_gid(const ob_grdb_t *db, unsigned min_gid, unsigned max_gid);
int ob_grdb_add_member(ob_grdb_t *db, int idx, const char *user);
int ob_grdb_remove_member(ob_grdb_t *db, int idx, const char *user);

uint64_t ob_fnv1a64(const char *s);
void ob_hash_password(const char *plain, char *out, size_t cap);
int ob_verify_password(const char *stored, const char *input);

int ob_name_valid(const char *name);

#endif
