/*
 * Obelisk — minimal login(1): authenticate against /etc/passwd, switch credentials, exec shell.
 * Must run as root (euid 0) so setuid/setgid to the target user can succeed.
 */

#include "../../lib/passwd_db.h"

#include <string.h>

typedef long ssize_t;

extern int geteuid(void);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern void _exit(int status);
extern int chdir(const char *path);
extern int setgid(int gid);
extern int setuid(int uid);
extern int execve(const char *pathname, char *const argv[], char *const envp[]);

#define PASSWD_PATH "/etc/passwd"

static void wrerr(const char *s) {
    (void)write(2, s, strlen(s));
}

static void join_kv(char *out, size_t cap, const char *key, const char *val) {
    size_t k = strlen(key);
    size_t v = strlen(val);
    size_t i = 0;
    size_t j;

    if (k + v + 1 >= cap) {
        return;
    }
    for (j = 0; j < k && i + 1 < cap; j++) {
        out[i++] = key[j];
    }
    for (j = 0; j < v && i + 1 < cap; j++) {
        out[i++] = val[j];
    }
    out[i] = '\0';
}

static int prompt_line(const char *p, char *buf, size_t cap) {
    ssize_t n;
    size_t len;

    (void)write(1, p, strlen(p));
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

static const char *env_get(char **envp, const char *key) {
    size_t kl = strlen(key);

    if (!envp) {
        return NULL;
    }
    for (; *envp; envp++) {
        if (strncmp(*envp, key, kl) == 0 && (*envp)[kl] == '=') {
            return *envp + kl + 1;
        }
    }
    return NULL;
}

int main(int argc, char **argv, char **envp) {
    ob_pwdb_t pw;
    int pi;
    char user[OB_PW_NAME_MAX];
    char passbuf[128];
    char *shargv[2];
    char env_home[OB_PW_HOME_MAX + 16];
    char env_user[OB_PW_NAME_MAX + 16];
    char env_shell[OB_PW_SHELL_MAX + 16];
    char env_path[] = "PATH=/bin:/sbin:/usr/bin";
    char env_term[80];
    const char *term;
    char *envp_new[16];
    int ei;
    const char *sh;

    (void)argc;
    (void)argv;
    if (geteuid() != 0) {
        wrerr("login: must be invoked as root (euid 0)\n");
        return 1;
    }
    if (prompt_line("login: ", user, sizeof(user)) < 0) {
        wrerr("login: read error\n");
        return 1;
    }
    if (prompt_line("Password: ", passbuf, sizeof(passbuf)) < 0) {
        wrerr("login: read error\n");
        return 1;
    }
    if (ob_pwdb_load(&pw, PASSWD_PATH) < 0) {
        wrerr("login: cannot read /etc/passwd\n");
        return 1;
    }
    pi = ob_pwdb_find_name(&pw, user);
    if (pi < 0) {
        wrerr("login: unknown user\n");
        return 1;
    }
    if (!ob_verify_password(pw.ent[pi].pass, passbuf)) {
        wrerr("login: incorrect password\n");
        return 1;
    }
    if (setgid((int)pw.ent[pi].gid) < 0) {
        wrerr("login: setgid failed\n");
        return 1;
    }
    if (setuid((int)pw.ent[pi].uid) < 0) {
        wrerr("login: setuid failed\n");
        return 1;
    }
    sh = pw.ent[pi].shell[0] ? pw.ent[pi].shell : "/bin/osh";
    if (pw.ent[pi].home[0] && chdir(pw.ent[pi].home) < 0) {
        wrerr("login: cannot change to home directory\n");
        return 1;
    }
    shargv[0] = (char *)sh;
    shargv[1] = NULL;
    join_kv(env_home, sizeof(env_home), "HOME=", pw.ent[pi].home[0] ? pw.ent[pi].home : "/");
    join_kv(env_user, sizeof(env_user), "USER=", pw.ent[pi].name);
    join_kv(env_shell, sizeof(env_shell), "SHELL=", sh);
    term = env_get(envp, "TERM");
    if (term && term[0]) {
        join_kv(env_term, sizeof(env_term), "TERM=", term);
    } else {
        join_kv(env_term, sizeof(env_term), "TERM=", "vt100");
    }
    env_term[sizeof(env_term) - 1] = '\0';
    ei = 0;
    envp_new[ei++] = env_path;
    envp_new[ei++] = env_home;
    envp_new[ei++] = env_user;
    envp_new[ei++] = env_shell;
    envp_new[ei++] = env_term;
    envp_new[ei] = NULL;
    execve(sh, shargv, envp_new);
    wrerr("login: exec failed\n");
    return 1;
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "lea 8(%rsi,%rdi,8), %rax\n"
        "lea 8(%rax), %rdx\n"
        "andq $-16, %rsp\n"
        "call main\n"
        "mov %eax, %edi\n"
        "call _exit\n");
}
