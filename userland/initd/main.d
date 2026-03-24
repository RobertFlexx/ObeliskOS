/*
 * Obelisk OS - obeliskd (D init system, -betterC)
 * From Axioms, Order.
 */

module initd.main;

extern(C) nothrow:

alias size_t = ulong;
alias ssize_t = long;
alias off_t = long;
alias pid_t = int;
alias uid_t = uint;
alias gid_t = uint;

enum SYS_SETUID = 105;
enum SYS_SETGID = 106;

enum O_RDONLY = 0x0000;

enum C_RESET = "\x1b[0m";
enum C_BOLD = "\x1b[1m";
enum C_DIM = "\x1b[2m";
enum C_RED = "\x1b[31m";
enum C_GREEN = "\x1b[32m";
enum C_YELLOW = "\x1b[33m";
enum C_BLUE = "\x1b[34m";
enum C_MAGENTA = "\x1b[35m";
enum C_CYAN = "\x1b[36m";

extern(C):
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int open(const char* pathname, int flags, int mode);
int close(int fd);
int fork();
int execve(const char* pathname, char** argv, char** envp);
int waitpid(int pid, int* status, int options);
int getpid();
int _exit(int status);
int setuid(int uid);
int setgid(int gid);

struct Config {
    char[64] profile;
    char[128] serviceList;
    char[128] shellPath;
    char[32] shellArg0;
    char[32] shellArg1;
    char[32] userName;
    uid_t uid;
    gid_t gid;
    int verbose;
}

struct Service {
    char[64] name;
    char[128] description;
    char[160] execCmd;
    char[32] userName;
    uid_t uid;
    gid_t gid;
    int autostart;
    int oneshot;
    int restartOnFailure;
}

private size_t cstrlen(const char* s) {
    size_t n = 0;
    while (s !is null && s[n] != 0) n++;
    return n;
}

private int cstrcmp(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] != 0 && b[i] != 0 && a[i] == b[i]) i++;
    return cast(int)(cast(ubyte)a[i]) - cast(int)(cast(ubyte)b[i]);
}

private int cstrncmp(const char* a, const char* b, size_t n) {
    size_t i = 0;
    while (i < n && a[i] != 0 && b[i] != 0 && a[i] == b[i]) i++;
    if (i == n) return 0;
    return cast(int)(cast(ubyte)a[i]) - cast(int)(cast(ubyte)b[i]);
}

private void cstrcpy(char* dst, const char* src, size_t cap) {
    if (cap == 0) return;
    size_t i = 0;
    while (i + 1 < cap && src[i] != 0) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

private int parseU32(const char* s, uint* outVal) {
    if (s is null || s[0] == 0) return -1;
    uint v = 0;
    size_t i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + cast(uint)(s[i] - '0');
        i++;
    }
    if (s[i] != 0) return -1;
    *outVal = v;
    return 0;
}

private int parseBool(const char* s) {
    if (s is null) return 0;
    if (cstrcmp(s, "1") == 0 || cstrcmp(s, "yes") == 0 || cstrcmp(s, "true") == 0 || cstrcmp(s, "on") == 0) return 1;
    return 0;
}

private void print(const char* s) {
    write(1, cast(const void*)s, cstrlen(s));
}

private void printPair(const char* a, const char* b) {
    print(a);
    print(b);
}

private void println(const char* s) {
    print(s);
    print("\n");
}

private void statusLine(const char* iconColor, const char* icon, const char* msg) {
    print(iconColor);
    print(icon);
    print(C_RESET);
    print(" ");
    println(msg);
}

private void banner() {
    println("");
    print(C_CYAN); print(C_BOLD); print("                /\\\n");
    print(C_CYAN); print(C_BOLD); print("               /  \\      obeliskd init\n");
    print(C_CYAN); print(C_BOLD); print("              / /\\ \\     From Axioms, Order.\n");
    print(C_CYAN); print(C_BOLD); print("             / ____ \\    modern service bootstrap\n");
    print(C_CYAN); print(C_BOLD); print("            /_/    \\_\\\n");
    printPair(C_DIM, "------------------------------------------------------------\n");
    printPair(C_RESET, "");
}

private void printU32(uint v) {
    char[16] tmp = void;
    int i = 15;
    tmp[i] = 0;
    if (v == 0) {
        tmp[--i] = '0';
    } else {
        while (v > 0 && i > 0) {
            tmp[--i] = cast(char)('0' + (v % 10));
            v /= 10;
        }
    }
    print(tmp.ptr + i);
}

private int readSmallFile(const char* path, char* outBuf, size_t cap) {
    if (cap == 0) return -1;
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off + 1 < cap) {
        auto n = read(fd, outBuf + off, cap - 1 - off);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) break;
        off += cast(size_t)n;
    }
    outBuf[off] = 0;
    close(fd);
    return cast(int)off;
}

private char* trim(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t n = cstrlen(s);
    while (n > 0) {
        auto c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[n - 1] = 0;
            n--;
        } else {
            break;
        }
    }
    return s;
}

private char* splitKV(char* line, char** outVal) {
    size_t i = 0;
    while (line[i] != 0) {
        if (line[i] == '=') {
            line[i] = 0;
            *outVal = trim(line + i + 1);
            return trim(line);
        }
        i++;
    }
    *outVal = null;
    return trim(line);
}

private void configDefaults(Config* cfg) {
    cstrcpy(cfg.profile.ptr, "production".ptr, cfg.profile.length);
    cstrcpy(cfg.serviceList.ptr, "/etc/obelisk/initd/services/motd.svc,/etc/obelisk/initd/services/identity.svc".ptr, cfg.serviceList.length);
    cstrcpy(cfg.shellPath.ptr, "/bin/busybox".ptr, cfg.shellPath.length);
    cstrcpy(cfg.shellArg0.ptr, "busybox".ptr, cfg.shellArg0.length);
    cstrcpy(cfg.shellArg1.ptr, "sh".ptr, cfg.shellArg1.length);
    cstrcpy(cfg.userName.ptr, "obelisk".ptr, cfg.userName.length);
    cfg.uid = 1000;
    cfg.gid = 1000;
    cfg.verbose = 1;
}

private void parseConfig(Config* cfg, char* content) {
    auto p = content;
    while (*p != 0) {
        auto line = p;
        while (*p != 0 && *p != '\n') p++;
        if (*p == '\n') {
            *p = 0;
            p++;
        }
        line = trim(line);
        if (*line == 0 || *line == '#') continue;

        char* value = null;
        auto key = splitKV(line, &value);
        if (value is null) continue;

        if (cstrcmp(key, "profile".ptr) == 0) cstrcpy(cfg.profile.ptr, value, cfg.profile.length);
        else if (cstrcmp(key, "services".ptr) == 0) cstrcpy(cfg.serviceList.ptr, value, cfg.serviceList.length);
        else if (cstrcmp(key, "shell".ptr) == 0) cstrcpy(cfg.shellPath.ptr, value, cfg.shellPath.length);
        else if (cstrcmp(key, "shell_arg0".ptr) == 0) cstrcpy(cfg.shellArg0.ptr, value, cfg.shellArg0.length);
        else if (cstrcmp(key, "shell_arg1".ptr) == 0) cstrcpy(cfg.shellArg1.ptr, value, cfg.shellArg1.length);
        else if (cstrcmp(key, "default_user".ptr) == 0) cstrcpy(cfg.userName.ptr, value, cfg.userName.length);
        else if (cstrcmp(key, "default_uid".ptr) == 0) { uint v; if (parseU32(value, &v) == 0) cfg.uid = v; }
        else if (cstrcmp(key, "default_gid".ptr) == 0) { uint v; if (parseU32(value, &v) == 0) cfg.gid = v; }
        else if (cstrcmp(key, "verbose".ptr) == 0) cfg.verbose = parseBool(value);
    }
}

private int lookupUserByName(const char* name, uid_t* outUid, gid_t* outGid) {
    char[4096] buf = void;
    if (readSmallFile("/etc/passwd".ptr, buf.ptr, buf.length) < 0) return -1;
    char* p = buf.ptr;
    while (*p != 0) {
        char* line = p;
        while (*p != 0 && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }
        line = trim(line);
        if (*line == 0 || *line == '#') continue;

        char*[7] fields = void;
        int nf = 0;
        char* t = line;
        fields[nf++] = t;
        while (*t != 0 && nf < 7) {
            if (*t == ':') {
                *t = 0;
                fields[nf++] = t + 1;
            }
            t++;
        }
        if (nf >= 4 && cstrcmp(fields[0], name) == 0) {
            uint u = 0, g = 0;
            if (parseU32(fields[2], &u) == 0 && parseU32(fields[3], &g) == 0) {
                *outUid = u;
                *outGid = g;
                return 0;
            }
        }
    }
    return -1;
}

private void serviceDefaults(Service* s) {
    cstrcpy(s.name.ptr, "unnamed".ptr, s.name.length);
    cstrcpy(s.description.ptr, "no description".ptr, s.description.length);
    s.execCmd[0] = 0;
    cstrcpy(s.userName.ptr, "root".ptr, s.userName.length);
    s.uid = 0;
    s.gid = 0;
    s.autostart = 1;
    s.oneshot = 1;
    s.restartOnFailure = 0;
}

private int loadService(const char* path, Service* svc) {
    char[4096] buf = void;
    if (readSmallFile(path, buf.ptr, buf.length) < 0) return -1;
    serviceDefaults(svc);
    auto p = buf.ptr;
    while (*p != 0) {
        auto line = p;
        while (*p != 0 && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }
        line = trim(line);
        if (*line == 0 || *line == '#') continue;
        char* value = null;
        auto key = splitKV(line, &value);
        if (value is null) continue;

        if (cstrcmp(key, "name".ptr) == 0) cstrcpy(svc.name.ptr, value, svc.name.length);
        else if (cstrcmp(key, "description".ptr) == 0) cstrcpy(svc.description.ptr, value, svc.description.length);
        else if (cstrcmp(key, "exec".ptr) == 0) cstrcpy(svc.execCmd.ptr, value, svc.execCmd.length);
        else if (cstrcmp(key, "autostart".ptr) == 0) svc.autostart = parseBool(value);
        else if (cstrcmp(key, "oneshot".ptr) == 0) svc.oneshot = parseBool(value);
        else if (cstrcmp(key, "restart".ptr) == 0) svc.restartOnFailure = (cstrcmp(value, "on-failure".ptr) == 0) ? 1 : 0;
        else if (cstrcmp(key, "user".ptr) == 0) cstrcpy(svc.userName.ptr, value, svc.userName.length);
        else if (cstrcmp(key, "uid".ptr) == 0) { uint v; if (parseU32(value, &v) == 0) svc.uid = v; }
        else if (cstrcmp(key, "gid".ptr) == 0) { uint v; if (parseU32(value, &v) == 0) svc.gid = v; }
    }

    if (svc.execCmd[0] == 0) return -1;
    if (svc.userName[0] != 0 && cstrcmp(svc.userName.ptr, "root".ptr) != 0) {
        uid_t uid;
        gid_t gid;
        if (lookupUserByName(svc.userName.ptr, &uid, &gid) == 0) {
            svc.uid = uid;
            svc.gid = gid;
        }
    }
    return 0;
}

private int launchCommandAs(const char* command, uid_t uid, gid_t gid, int waitIt) {
    auto pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setgid(cast(int)gid);
        setuid(cast(int)uid);
        char*[4] argv = [ cast(char*)"/bin/sh".ptr, cast(char*)"-c".ptr, cast(char*)command, null ];
        char*[4] envp = [ cast(char*)"PATH=/bin:/sbin:/usr/bin".ptr, cast(char*)"HOME=/".ptr, cast(char*)"TERM=vt100".ptr, null ];
        execve("/bin/sh".ptr, argv.ptr, envp.ptr);
        _exit(127);
    }
    if (waitIt != 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) { }
    }
    return 0;
}

private void runServices(const Config* cfg) {
    char[256] list = void;
    cstrcpy(list.ptr, cfg.serviceList.ptr, list.length);
    char* p = list.ptr;
    statusLine(C_MAGENTA, "[*]".ptr, "Launching service graph".ptr);
    while (*p != 0) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (*p == 0) break;
        char* start = p;
        while (*p != 0 && *p != ',') p++;
        char hold = *p;
        *p = 0;
        char* svcPath = trim(start);
        if (*svcPath != 0) {
            Service svc = void;
            if (loadService(svcPath, &svc) == 0 && svc.autostart != 0) {
                print(C_CYAN); print("  -> "); print(C_RESET); print(svc.name.ptr); print(" ");
                print(C_DIM); print("("); print(svc.description.ptr); print(")"); println(C_RESET);
                if (launchCommandAs(svc.execCmd.ptr, svc.uid, svc.gid, svc.oneshot) == 0) {
                    statusLine(C_GREEN, "[ok]".ptr, "service started".ptr);
                } else {
                    statusLine(C_RED, "[!!]".ptr, "service failed to launch".ptr);
                }
            }
        }
        if (hold == 0) break;
        *p = hold;
        p++;
    }
}

private void launchInteractive(const Config* cfg) {
    uid_t uid = cfg.uid;
    gid_t gid = cfg.gid;
    if (cfg.userName[0] != 0 && cstrcmp(cfg.userName.ptr, "root".ptr) != 0) {
        uid_t u;
        gid_t g;
        if (lookupUserByName(cfg.userName.ptr, &u, &g) == 0) {
            uid = u;
            gid = g;
        }
    }

    statusLine(C_BLUE, "[*]".ptr, "Activating interactive session".ptr);
    setgid(cast(int)gid);
    setuid(cast(int)uid);

    char*[6] envp = [
        cast(char*)"PATH=/bin:/sbin:/usr/bin".ptr,
        cast(char*)"HOME=/home/obelisk".ptr,
        cast(char*)"TERM=vt100".ptr,
        cast(char*)"SHELL=/bin/sh".ptr,
        cast(char*)"USER=obelisk".ptr,
        null
    ];

    char*[3] argv = [ cast(char*)cfg.shellArg0.ptr, cast(char*)cfg.shellArg1.ptr, null ];
    if (execve(cfg.shellPath.ptr, argv.ptr, envp.ptr) < 0) {
        statusLine(C_RED, "[!!]".ptr, "primary shell failed, trying /sbin/init-legacy".ptr);
        char*[2] fargv = [ cast(char*)"init-legacy".ptr, null ];
        execve("/sbin/init-legacy".ptr, fargv.ptr, envp.ptr);
        statusLine(C_RED, "[!!]".ptr, "legacy init failed, trying /bin/busybox sh".ptr);
        char*[3] bbargv = [ cast(char*)"busybox".ptr, cast(char*)"sh".ptr, null ];
        execve("/bin/busybox".ptr, bbargv.ptr, envp.ptr);
    }
}

extern(C) void _start() {
    banner();
    statusLine(C_GREEN, "[ok]".ptr, "obeliskd starting".ptr);

    Config cfg = void;
    configDefaults(&cfg);

    char[4096] cfgBuf = void;
    if (readSmallFile("/etc/obelisk/initd.conf".ptr, cfgBuf.ptr, cfgBuf.length) >= 0) {
        parseConfig(&cfg, cfgBuf.ptr);
        statusLine(C_GREEN, "[ok]".ptr, "loaded /etc/obelisk/initd.conf".ptr);
    } else {
        statusLine(C_YELLOW, "[..]".ptr, "using built-in defaults (no initd.conf)".ptr);
    }

    if (cfg.verbose != 0) {
        print(C_DIM);
        print("profile="); print(cfg.profile.ptr);
        print(" user="); print(cfg.userName.ptr);
        print(" uid/gid=");
        printU32(cfg.uid);
        print("/");
        printU32(cfg.gid);
        println(C_RESET);
    }

    runServices(&cfg);
    launchInteractive(&cfg);

    _exit(1);
}
