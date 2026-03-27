/*
 * Shared helpers for account-management tools (useradd, usermod, userdel, passwd).
 */

#ifndef OB_ACCOUNT_COMMON_H
#define OB_ACCOUNT_COMMON_H

#include <stddef.h>

#define ACCT_PATH_PASSWD "/etc/passwd"
#define ACCT_PATH_GROUP "/etc/group"
#define ACCT_MIN_UID 1000u
#define ACCT_MAX_UID 60000u
#define ACCT_MIN_GID 1000u
#define ACCT_MAX_GID 60000u
#define ACCT_DEF_SHELL "/bin/osh"

int acct_require_root(void);
int acct_mkdir_p(const char *path, int mode);
int acct_chown_tree(const char *path, int uid, int gid);
int acct_prompt_password(const char *prompt, char *buf, size_t cap);
int acct_rm_tree(const char *path);

#endif
