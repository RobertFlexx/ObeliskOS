/*
 * Obelisk OS - opkg core (D, -betterC static mode)
 * From Axioms, Order.
 */

module bin.opkg.opkg_d;

extern(C) nothrow:

extern int printf(const char* fmt, ...);
extern int strcmp(const char* s1, const char* s2);
extern long write(int fd, const void* buf, ulong count);
extern long read(int fd, void* buf, ulong count);
extern int open(const char* pathname, int flags, int mode);
extern int close(int fd);
extern int fork();
extern int execve(const char* pathname, char** argv, char** envp);
extern int waitpid(int pid, int* status, int options);
extern int getpid();
extern int mkdir(const char* pathname, int mode);
extern int unlink(const char* pathname);
extern int pipe(int* pipefd);
extern int dup2(int oldfd, int newfd);
extern int socket(int domain, int type, int protocol);
extern int _exit(int status);

enum O_RDONLY = 0;
enum O_WRONLY = 1;
enum O_CREAT = 64;
enum O_TRUNC = 512;
enum AF_INET = 2;
enum SOCK_STREAM = 1;
enum SOCK_DGRAM = 2;

enum MAX_FILE = 32768;
enum MAX_ITEMS = 512;
enum TAR_BLOCK = 512;

struct OwnerEntry {
    char[256] path;
    char[64] pkg;
}

struct RepoConfigEntry {
    char[64] name;
    char[256] base;
}

struct RepoIndexEntry {
    char[128] name;
    char[64] ver;
    char[64] arch;
    char[256] filename;
    char[96] checksum;
    char[256] summary;
}

private ulong cstrlen(const char* s) {
    ulong n = 0;
    if (s is null) return 0;
    while (s[n] != 0) n++;
    return n;
}

private int startsWith(const char* s, const char* pre) {
    ulong i = 0;
    if (s is null || pre is null) return 0;
    while (pre[i] != 0) {
        if (s[i] != pre[i]) return 0;
        i++;
    }
    return 1;
}

private int containsSubstr(const char* s, const char* needle) {
    if (s is null || needle is null) return 0;
    if (needle[0] == 0) return 1;
    for (ulong i = 0; s[i] != 0; i++) {
        ulong j = 0;
        while (needle[j] != 0 && s[i + j] != 0 && s[i + j] == needle[j]) {
            j++;
        }
        if (needle[j] == 0) return 1;
    }
    return 0;
}

private int streq(const char* a, const char* b) {
    return strcmp(a, b) == 0;
}

private int endsWith(const char* s, const char* suf) {
    ulong n = cstrlen(s);
    ulong m = cstrlen(suf);
    if (m > n) return 0;
    for (ulong i = 0; i < m; i++) {
        if (s[n - m + i] != suf[i]) return 0;
    }
    return 1;
}

private void copyStr(char* dst, const char* src, ulong cap) {
    if (cap == 0) return;
    ulong i = 0;
    while (i + 1 < cap && src !is null && src[i] != 0) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

private void appendStr(char* dst, const char* src, ulong cap) {
    ulong i = cstrlen(dst);
    ulong j = 0;
    if (i >= cap) return;
    while (i + 1 < cap && src !is null && src[j] != 0) {
        dst[i++] = src[j++];
    }
    dst[i] = 0;
}

private void normalizePath(char* s) {
    if (s[0] == '.' && s[1] == '/') {
        ulong i = 2;
        ulong j = 0;
        while (s[i] != 0) s[j++] = s[i++];
        s[j] = 0;
    }
    if (s[0] != '/') {
        ulong n = cstrlen(s);
        if (n + 1 < 255) {
            for (long i = cast(long)n; i >= 0; i--) {
                s[i + 1] = s[i];
            }
            s[0] = '/';
        }
    }
}

private int pathHasTraversal(const char* s) {
    if (s is null) return 1;
    if (s[0] == 0) return 1;
    if (startsWith(s, "/".ptr) != 0) return 1;
    if (containsSubstr(s, "..".ptr) == 0) return 0;

    ulong i = 0;
    while (s[i] != 0) {
        while (s[i] == '/') i++;
        if (s[i] == 0) break;
        ulong start = i;
        while (s[i] != 0 && s[i] != '/') i++;
        ulong len = i - start;
        if (len == 2 && s[start] == '.' && s[start + 1] == '.') {
            return 1;
        }
    }
    return 0;
}

private int isSafePkgName(const char* name) {
    if (name is null || name[0] == 0) return 0;
    for (ulong i = 0; name[i] != 0; i++) {
        auto c = name[i];
        int ok = ((c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-' || c == '_' || c == '.' || c == '+');
        if (ok == 0) return 0;
    }
    return 1;
}

private int readFile(const char* path, char* dst, ulong cap) {
    if (cap == 0) return -1;
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    ulong off = 0;
    while (off + 1 < cap) {
        auto n = read(fd, dst + off, cap - 1 - off);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) break;
        off += cast(ulong)n;
    }
    dst[off] = 0;
    close(fd);
    return cast(int)off;
}

private int writeFile(const char* path, const char* data) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 420);
    if (fd < 0) return -1;
    ulong n = cstrlen(data);
    auto w = write(fd, data, n);
    close(fd);
    return (w == cast(long)n) ? 0 : -1;
}

private ulong parseOctal(const char* p, int n) {
    ulong v = 0;
    for (int i = 0; i < n; i++) {
        auto c = p[i];
        if (c == 0 || c == ' ' || c == '\t') continue;
        if (c < '0' || c > '7') break;
        v = (v << 3) + cast(ulong)(c - '0');
    }
    return v;
}

private int readExact(int fd, void* buf, ulong n) {
    ulong off = 0;
    while (off < n) {
        auto r = read(fd, cast(ubyte*)buf + off, n - off);
        if (r <= 0) return -1;
        off += cast(ulong)r;
    }
    return 0;
}

private int writeExact(int fd, const void* buf, ulong n) {
    ulong off = 0;
    while (off < n) {
        auto w = write(fd, cast(const(ubyte)*)buf + off, n - off);
        if (w <= 0) return -1;
        off += cast(ulong)w;
    }
    return 0;
}

private int skipBytes(int fd, ulong n) {
    ubyte[512] scratch = void;
    ulong rem = n;
    while (rem > 0) {
        ulong chunk = rem > scratch.length ? scratch.length : rem;
        if (readExact(fd, scratch.ptr, chunk) < 0) return -1;
        rem -= chunk;
    }
    return 0;
}

private int isZeroBlock(const(ubyte)* b) {
    for (int i = 0; i < TAR_BLOCK; i++) {
        if (b[i] != 0) return 0;
    }
    return 1;
}

private void copyTarName(char* dst, ulong cap, const(ubyte)* hdr) {
    ulong i = 0;
    while (i + 1 < cap && i < 100 && hdr[i] != 0) {
        dst[i] = cast(char)hdr[i];
        i++;
    }
    dst[i] = 0;
}

private void ensureParentDirs(const char* path) {
    char[256] tmp = void;
    copyStr(tmp.ptr, path, tmp.length);
    ulong n = cstrlen(tmp.ptr);
    for (ulong i = 1; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = 0;
            mkdir(tmp.ptr, 493);
            tmp[i] = '/';
        }
    }
}

private int extractFilesTar(const char* tarPath, char[256]* files, int maxFiles) {
    int fd = open(tarPath, O_RDONLY, 0);
    if (fd < 0) return -1;
    ubyte[TAR_BLOCK] hdr = void;
    int fileCount = 0;

    while (true) {
        if (readExact(fd, hdr.ptr, TAR_BLOCK) < 0) {
            close(fd);
            return -1;
        }
        if (isZeroBlock(hdr.ptr) != 0) break;

        char[256] name = void;
        copyTarName(name.ptr, name.length, hdr.ptr);
        if (pathHasTraversal(name.ptr) != 0) {
            close(fd);
            return -1;
        }
        auto size = parseOctal(cast(const(char)*)hdr.ptr + 124, 12);
        auto typeflag = cast(char)hdr[156];
        ulong mode = parseOctal(cast(const(char)*)hdr.ptr + 100, 8);
        if (mode == 0) mode = 420;

        if (typeflag == '5') {
            normalizePath(name.ptr);
            mkdir(name.ptr, cast(int)mode);
            ulong pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
            if (skipBytes(fd, size + pad) < 0) {
                close(fd);
                return -1;
            }
            continue;
        }

        normalizePath(name.ptr);
        ensureParentDirs(name.ptr);
        int outfd = open(name.ptr, O_WRONLY | O_CREAT | O_TRUNC, cast(int)mode);
        if (outfd < 0) {
            close(fd);
            return -1;
        }
        ubyte[512] chunk = void;
        ulong rem = size;
        while (rem > 0) {
            ulong now = rem > chunk.length ? chunk.length : rem;
            if (readExact(fd, chunk.ptr, now) < 0 || writeExact(outfd, chunk.ptr, now) < 0) {
                close(outfd);
                close(fd);
                return -1;
            }
            rem -= now;
        }
        close(outfd);
        if (fileCount < maxFiles && cstrlen(name.ptr) > 0) {
            copyStr(files[fileCount].ptr, name.ptr, files[fileCount].length);
            fileCount++;
        }

        ulong pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
        if (pad > 0 && skipBytes(fd, pad) < 0) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    return fileCount;
}

private int unpackOpk(const char* opkPath, const char* filesTarOut, char* metaOut, ulong metaCap) {
    int fd = open(opkPath, O_RDONLY, 0);
    if (fd < 0) return -1;
    int outfd = open(filesTarOut, O_WRONLY | O_CREAT | O_TRUNC, 420);
    if (outfd < 0) {
        close(fd);
        return -1;
    }

    ubyte[TAR_BLOCK] hdr = void;
    int gotMeta = 0;
    int gotFilesTar = 0;
    metaOut[0] = 0;

    while (true) {
        if (readExact(fd, hdr.ptr, TAR_BLOCK) < 0) {
            close(outfd);
            close(fd);
            return -1;
        }
        if (isZeroBlock(hdr.ptr) != 0) break;

        char[256] name = void;
        copyTarName(name.ptr, name.length, hdr.ptr);
        if (streq(name.ptr, "meta.json".ptr) == 0 && streq(name.ptr, "files.tar".ptr) == 0) {
            close(outfd);
            close(fd);
            return -1;
        }
        auto size = parseOctal(cast(const(char)*)hdr.ptr + 124, 12);

        if (streq(name.ptr, "meta.json".ptr)) {
            ulong toRead = size < (metaCap - 1) ? size : (metaCap - 1);
            if (readExact(fd, metaOut, toRead) < 0) {
                close(outfd);
                close(fd);
                return -1;
            }
            metaOut[toRead] = 0;
            if (size > toRead && skipBytes(fd, size - toRead) < 0) {
                close(outfd);
                close(fd);
                return -1;
            }
            gotMeta = 1;
        } else if (streq(name.ptr, "files.tar".ptr)) {
            ubyte[512] chunk = void;
            ulong rem = size;
            while (rem > 0) {
                ulong now = rem > chunk.length ? chunk.length : rem;
                if (readExact(fd, chunk.ptr, now) < 0 || writeExact(outfd, chunk.ptr, now) < 0) {
                    close(outfd);
                    close(fd);
                    return -1;
                }
                rem -= now;
            }
            gotFilesTar = 1;
        } else {
            if (skipBytes(fd, size) < 0) {
                close(outfd);
                close(fd);
                return -1;
            }
        }

        ulong pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
        if (pad > 0 && skipBytes(fd, pad) < 0) {
            close(outfd);
            close(fd);
            return -1;
        }
    }

    close(outfd);
    close(fd);
    if (gotMeta == 0 || gotFilesTar == 0) return -1;
    return 0;
}

private int runExec(const char* prog, char** argv) {
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char*[2] envp = [ cast(char*)"PATH=/bin:/sbin:/usr/bin".ptr, null ];
        execve(prog, argv, envp.ptr);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) { }
    return status == 0 ? 0 : -1;
}

private int runExecCapture(const char* prog, char** argv, char* dst, ulong cap) {
    if (cap == 0) return -1;
    int[2] pfd = void;
    if (pipe(pfd.ptr) < 0) return -1;
    int pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], 1);
        dup2(pfd[1], 2);
        close(pfd[1]);
        char*[2] envp = [ cast(char*)"PATH=/bin:/sbin:/usr/bin".ptr, null ];
        execve(prog, argv, envp.ptr);
        _exit(127);
    }
    close(pfd[1]);
    ulong off = 0;
    while (off + 1 < cap) {
        auto n = read(pfd[0], dst + off, cap - 1 - off);
        if (n <= 0) break;
        off += cast(ulong)n;
    }
    dst[off] = 0;
    close(pfd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) { }
    return status == 0 ? 0 : -1;
}

private const(char)* findKey(const char* json, const char* key) {
    ulong jn = cstrlen(json);
    ulong kn = cstrlen(key);
    for (ulong i = 0; i + kn + 2 < jn; i++) {
        if (json[i] == '"') {
            ulong k = 0;
            while (k < kn && json[i + 1 + k] == key[k]) k++;
            if (k == kn && json[i + 1 + kn] == '"') {
                return json + i + 1 + kn + 1;
            }
        }
    }
    return null;
}

private int extractJsonString(const char* json, const char* key, char* dst, ulong cap) {
    auto p = findKey(json, key);
    if (p is null) return -1;
    while (*p != 0 && *p != ':') p++;
    if (*p == 0) return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return -1;
    p++;
    ulong i = 0;
    while (*p != 0 && *p != '"' && i + 1 < cap) {
        dst[i++] = *p++;
    }
    dst[i] = 0;
    return (*p == '"') ? 0 : -1;
}

private int extractJsonArrayStrings(const char* json, const char* key, char[256]* items, int maxItems) {
    auto p = findKey(json, key);
    if (p is null) return 0;
    while (*p != 0 && *p != ':') p++;
    if (*p == 0) return 0;
    p++;
    while (*p != 0 && *p != '[') p++;
    if (*p != '[') return 0;
    p++;
    int count = 0;
    while (*p != 0 && *p != ']') {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == '"') {
            p++;
            ulong i = 0;
            while (*p != 0 && *p != '"' && i + 1 < 256) {
                items[count][i++] = *p++;
            }
            items[count][i] = 0;
            if (*p == '"') p++;
            count++;
            if (count >= maxItems) break;
        } else {
            p++;
        }
    }
    return count;
}

private int parseTarList(const char* text, char[256]* files, int maxFiles) {
    int count = 0;
    ulong i = 0;
    while (text[i] != 0) {
        char[256] line = void;
        ulong j = 0;
        while (text[i] != 0 && text[i] != '\n' && j + 1 < line.length) {
            line[j++] = text[i++];
        }
        line[j] = 0;
        if (text[i] == '\n') i++;
        if (j == 0) continue;
        if (line[j - 1] == '/') continue;
        normalizePath(line.ptr);
        copyStr(files[count].ptr, line.ptr, files[count].length);
        count++;
        if (count >= maxFiles) break;
    }
    return count;
}

private int loadOwners(const char* path, OwnerEntry* owners, int maxOwners) {
    char[MAX_FILE] buf = void;
    if (readFile(path, buf.ptr, buf.length) < 0) return 0;
    auto p = findKey(buf.ptr, "owners".ptr);
    if (p is null) return 0;
    while (*p != 0 && *p != '{') p++;
    if (*p != '{') return 0;
    p++;
    int n = 0;
    while (*p != 0 && *p != '}') {
        while (*p != 0 && *p != '"') p++;
        if (*p != '"') break;
        p++;
        ulong i = 0;
        while (*p != 0 && *p != '"' && i + 1 < owners[n].path.length) owners[n].path[i++] = *p++;
        owners[n].path[i] = 0;
        if (*p == '"') p++;
        while (*p != 0 && *p != ':') p++;
        if (*p == ':') p++;
        while (*p != 0 && *p != '"') p++;
        if (*p != '"') break;
        p++;
        i = 0;
        while (*p != 0 && *p != '"' && i + 1 < owners[n].pkg.length) owners[n].pkg[i++] = *p++;
        owners[n].pkg[i] = 0;
        if (*p == '"') p++;
        n++;
        if (n >= maxOwners) break;
        while (*p != 0 && *p != ',' && *p != '}') p++;
        if (*p == ',') p++;
    }
    return n;
}

private int saveOwners(const char* path, OwnerEntry* owners, int n) {
    char[MAX_FILE] text = void;
    copyStr(text.ptr, "{\n  \"owners\": {\n".ptr, text.length);
    for (int i = 0; i < n; i++) {
        appendStr(text.ptr, "    \"".ptr, text.length);
        appendStr(text.ptr, owners[i].path.ptr, text.length);
        appendStr(text.ptr, "\": \"".ptr, text.length);
        appendStr(text.ptr, owners[i].pkg.ptr, text.length);
        appendStr(text.ptr, "\"".ptr, text.length);
        if (i + 1 < n) appendStr(text.ptr, ",".ptr, text.length);
        appendStr(text.ptr, "\n".ptr, text.length);
    }
    appendStr(text.ptr, "  }\n}\n".ptr, text.length);
    return writeFile(path, text.ptr);
}

private int ensureDbDirs() {
    mkdir("/var".ptr, 493);
    mkdir("/var/lib".ptr, 493);
    mkdir("/var/lib/opkg".ptr, 493);
    mkdir("/var/lib/opkg/installed".ptr, 493);
    mkdir("/var/lib/opkg/repos".ptr, 493);
    mkdir("/var/lib/opkg/cache".ptr, 493);
    return 0;
}

private int parseRepoConfig(RepoConfigEntry* repos, int maxRepos) {
    char[8192] text = void;
    if (readFile("/etc/opkg/repos.conf".ptr, text.ptr, text.length) < 0) {
        return 0;
    }
    int count = 0;
    ulong i = 0;
    while (text[i] != 0) {
        while (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n') i++;
        if (text[i] == '#') {
            while (text[i] != 0 && text[i] != '\n') i++;
            continue;
        }
        if (text[i] == 0) break;

        char[64] name = void;
        char[256] base = void;
        ulong n = 0;
        while (text[i] != 0 && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && n + 1 < name.length) {
            name[n++] = text[i++];
        }
        name[n] = 0;
        while (text[i] == ' ' || text[i] == '\t') i++;
        ulong b = 0;
        while (text[i] != 0 && text[i] != '\r' && text[i] != '\n' && b + 1 < base.length) {
            base[b++] = text[i++];
        }
        base[b] = 0;
        while (text[i] == '\r' || text[i] == '\n') i++;

        if (name[0] == 0 || base[0] == 0) continue;
        copyStr(repos[count].name.ptr, name.ptr, repos[count].name.length);
        copyStr(repos[count].base.ptr, base.ptr, repos[count].base.length);
        count++;
        if (count >= maxRepos) break;
    }
    return count;
}

private int parseRepoIndexJson(const char* json, RepoIndexEntry* outEntries, int maxEntries) {
    int count = 0;
    ulong i = 0;
    while (json[i] != 0 && count < maxEntries) {
        if (json[i] != '{') {
            i++;
            continue;
        }
        ulong start = i;
        int depth = 0;
        while (json[i] != 0) {
            if (json[i] == '{') depth++;
            if (json[i] == '}') {
                depth--;
                if (depth == 0) {
                    i++;
                    break;
                }
            }
            i++;
        }
        if (depth != 0) break;

        char[4096] obj = void;
        ulong len = i - start;
        if (len >= obj.length) len = obj.length - 1;
        for (ulong k = 0; k < len; k++) obj[k] = json[start + k];
        obj[len] = 0;

        if (extractJsonString(obj.ptr, "name".ptr, outEntries[count].name.ptr, outEntries[count].name.length) < 0) continue;
        if (extractJsonString(obj.ptr, "version".ptr, outEntries[count].ver.ptr, outEntries[count].ver.length) < 0) continue;
        if (extractJsonString(obj.ptr, "arch".ptr, outEntries[count].arch.ptr, outEntries[count].arch.length) < 0) continue;
        if (extractJsonString(obj.ptr, "filename".ptr, outEntries[count].filename.ptr, outEntries[count].filename.length) < 0) continue;
        extractJsonString(obj.ptr, "checksum".ptr, outEntries[count].checksum.ptr, outEntries[count].checksum.length);
        extractJsonString(obj.ptr, "summary".ptr, outEntries[count].summary.ptr, outEntries[count].summary.length);
        count++;
    }
    return count;
}

private int versionCmp(const char* a, const char* b) {
    ulong ia = 0;
    ulong ib = 0;
    while (a[ia] != 0 || b[ib] != 0) {
        ulong va = 0;
        ulong vb = 0;
        while (a[ia] >= '0' && a[ia] <= '9') {
            va = va * 10 + cast(ulong)(a[ia] - '0');
            ia++;
        }
        while (b[ib] >= '0' && b[ib] <= '9') {
            vb = vb * 10 + cast(ulong)(b[ib] - '0');
            ib++;
        }
        if (va < vb) return -1;
        if (va > vb) return 1;
        if (a[ia] == '.') ia++;
        if (b[ib] == '.') ib++;
        if ((a[ia] == 0 && b[ib] == 0)) break;
    }
    return 0;
}

private int isInstalled(const char* pkg) {
    char[320] path = void;
    copyStr(path.ptr, "/var/lib/opkg/installed/".ptr, path.length);
    appendStr(path.ptr, pkg, path.length);
    appendStr(path.ptr, ".json".ptr, path.length);
    int fd = open(path.ptr, O_RDONLY, 0);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

private int writeInstalledRecord(const char* pkg, const char* metaJson, char[256]* files, int fileCount) {
    char[320] path = void;
    char[MAX_FILE] text = void;
    copyStr(path.ptr, "/var/lib/opkg/installed/".ptr, path.length);
    appendStr(path.ptr, pkg, path.length);
    appendStr(path.ptr, ".json".ptr, path.length);

    copyStr(text.ptr, "{\n  \"meta\": ".ptr, text.length);
    appendStr(text.ptr, metaJson, text.length);
    appendStr(text.ptr, ",\n  \"installed_files\": [\n".ptr, text.length);
    for (int i = 0; i < fileCount; i++) {
        appendStr(text.ptr, "    \"".ptr, text.length);
        appendStr(text.ptr, files[i].ptr, text.length);
        appendStr(text.ptr, "\"".ptr, text.length);
        if (i + 1 < fileCount) appendStr(text.ptr, ",".ptr, text.length);
        appendStr(text.ptr, "\n".ptr, text.length);
    }
    appendStr(text.ptr, "  ]\n}\n".ptr, text.length);
    return writeFile(path.ptr, text.ptr);
}

private int installLocal(const char* opkPath) {
    ensureDbDirs();

    char[256] filesTar = void;
    copyStr(filesTar.ptr, "/tmp/opkg-files.tar".ptr, filesTar.length);
    char[MAX_FILE] meta = void;
    if (unpackOpk(opkPath, filesTar.ptr, meta.ptr, meta.length) < 0) {
        printf("opkg install: malformed package (missing meta.json/files.tar)\n");
        return 1;
    }

    char[128] pkg = void;
    char[64] ver = void;
    char[64] arch = void;
    if (extractJsonString(meta.ptr, "name".ptr, pkg.ptr, pkg.length) < 0 ||
        extractJsonString(meta.ptr, "version".ptr, ver.ptr, ver.length) < 0 ||
        extractJsonString(meta.ptr, "arch".ptr, arch.ptr, arch.length) < 0) {
        printf("opkg install: malformed package metadata\n");
        return 1;
    }
    if (isSafePkgName(pkg.ptr) == 0) {
        printf("opkg install: invalid package name in metadata\n");
        return 1;
    }
    if (streq(arch.ptr, "x86_64".ptr) == 0 && streq(arch.ptr, "noarch".ptr) == 0) {
        printf("opkg install: unsupported package arch: %s\n", arch.ptr);
        return 1;
    }
    if (isInstalled(pkg.ptr) != 0) {
        printf("opkg install: package already installed: %s\n", pkg.ptr);
        return 1;
    }

    char[256][MAX_ITEMS] files = void;
    int fileCount = extractFilesTar(filesTar.ptr, files.ptr, MAX_ITEMS);
    if (fileCount < 0) {
        printf("opkg install: failed to extract files.tar payload\n");
        return 1;
    }

    OwnerEntry[MAX_ITEMS] owners = void;
    int ownerCount = loadOwners("/var/lib/opkg/fileowners.json".ptr, owners.ptr, owners.length);
    for (int i = 0; i < fileCount; i++) {
        for (int j = 0; j < ownerCount; j++) {
            if (streq(files[i].ptr, owners[j].path.ptr) && !streq(owners[j].pkg.ptr, pkg.ptr)) {
                printf("opkg install: file conflict: %s owned by %s\n", files[i].ptr, owners[j].pkg.ptr);
                return 1;
            }
        }
    }

    if (writeInstalledRecord(pkg.ptr, meta.ptr, files.ptr, fileCount) != 0) {
        printf("opkg install: failed to write installed record\n");
        return 1;
    }

    for (int i = 0; i < fileCount && ownerCount < owners.length; i++) {
        int found = 0;
        for (int j = 0; j < ownerCount; j++) {
            if (streq(files[i].ptr, owners[j].path.ptr)) {
                copyStr(owners[j].pkg.ptr, pkg.ptr, owners[j].pkg.length);
                found = 1;
                break;
            }
        }
        if (found == 0) {
            copyStr(owners[ownerCount].path.ptr, files[i].ptr, owners[ownerCount].path.length);
            copyStr(owners[ownerCount].pkg.ptr, pkg.ptr, owners[ownerCount].pkg.length);
            ownerCount++;
        }
    }
    if (saveOwners("/var/lib/opkg/fileowners.json".ptr, owners.ptr, ownerCount) != 0) {
        printf("opkg install: failed to update file owner database\n");
        return 1;
    }

    printf("Installed %s %s (%s)\n", pkg.ptr, ver.ptr, arch.ptr);
    return 0;
}

private int loadInstalledRecord(const char* pkg, char* metaOut, ulong metaCap, char[256]* files, int maxFiles, int* outCount) {
    char[320] path = void;
    char[MAX_FILE] rec = void;
    copyStr(path.ptr, "/var/lib/opkg/installed/".ptr, path.length);
    appendStr(path.ptr, pkg, path.length);
    appendStr(path.ptr, ".json".ptr, path.length);
    if (readFile(path.ptr, rec.ptr, rec.length) < 0) return -1;

    auto m = findKey(rec.ptr, "meta".ptr);
    if (m is null) return -1;
    while (*m != 0 && *m != ':') m++;
    if (*m == 0) return -1;
    m++;
    while (*m == ' ' || *m == '\n' || *m == '\r' || *m == '\t') m++;
    if (*m != '{') return -1;
    int depth = 0;
    ulong i = 0;
    while (*m != 0 && i + 1 < metaCap) {
        metaOut[i++] = *m;
        if (*m == '{') depth++;
        if (*m == '}') {
            depth--;
            if (depth == 0) {
                m++;
                break;
            }
        }
        m++;
    }
    metaOut[i] = 0;
    *outCount = extractJsonArrayStrings(rec.ptr, "installed_files".ptr, files, maxFiles);
    return 0;
}

private int cmdRemove(const char* pkg) {
    if (isSafePkgName(pkg) == 0) {
        printf("opkg remove: invalid package name\n");
        return 1;
    }
    char[8192] meta = void;
    char[256][MAX_ITEMS] files = void;
    int fileCount = 0;
    if (loadInstalledRecord(pkg, meta.ptr, meta.length, files.ptr, files.length, &fileCount) < 0) {
        printf("opkg remove: package is not installed: %s\n", pkg);
        return 1;
    }

    OwnerEntry[MAX_ITEMS] owners = void;
    int ownerCount = loadOwners("/var/lib/opkg/fileowners.json".ptr, owners.ptr, owners.length);
    for (int i = 0; i < fileCount; i++) {
        for (int j = 0; j < ownerCount; j++) {
            if (streq(files[i].ptr, owners[j].path.ptr) && !streq(owners[j].pkg.ptr, pkg)) {
                printf("opkg remove: refusing to remove %s (owned by %s)\n", files[i].ptr, owners[j].pkg.ptr);
                return 1;
            }
        }
    }

    for (int i = 0; i < fileCount; i++) {
        int ur = unlink(files[i].ptr);
        if (ur != 0) {
            printf("opkg remove: warning: failed to remove %s (%d)\n", files[i].ptr, ur);
        }
    }

    int newCount = 0;
    OwnerEntry[MAX_ITEMS] newOwners = void;
    for (int i = 0; i < ownerCount; i++) {
        if (!streq(owners[i].pkg.ptr, pkg)) {
            copyStr(newOwners[newCount].path.ptr, owners[i].path.ptr, newOwners[newCount].path.length);
            copyStr(newOwners[newCount].pkg.ptr, owners[i].pkg.ptr, newOwners[newCount].pkg.length);
            newCount++;
        }
    }
    if (saveOwners("/var/lib/opkg/fileowners.json".ptr, newOwners.ptr, newCount) != 0) {
        printf("opkg remove: failed to update file owner database\n");
        return 1;
    }

    char[320] path = void;
    copyStr(path.ptr, "/var/lib/opkg/installed/".ptr, path.length);
    appendStr(path.ptr, pkg, path.length);
    appendStr(path.ptr, ".json".ptr, path.length);
    unlink(path.ptr);
    printf("Removed %s\n", pkg);
    return 0;
}

private int cmdList() {
    char[MAX_FILE] text = void;
    char*[4] lsArgs = [ cast(char*)"/bin/ls".ptr, cast(char*)"/var/lib/opkg/installed".ptr, null, null ];
    if (runExecCapture("/bin/ls".ptr, lsArgs.ptr, text.ptr, text.length) != 0) {
        printf("No packages installed.\n");
        return 0;
    }
    ulong i = 0;
    int shown = 0;
    while (text[i] != 0) {
        char[256] tok = void;
        ulong j = 0;
        while (text[i] == ' ' || text[i] == '\n' || text[i] == '\t' || text[i] == '\r') i++;
        while (text[i] != 0 && text[i] != ' ' && text[i] != '\n' && text[i] != '\t' && text[i] != '\r' && j + 1 < tok.length) tok[j++] = text[i++];
        tok[j] = 0;
        if (j == 0) break;
        if (endsWith(tok.ptr, ".json".ptr) != 0) {
            tok[j - 5] = 0;
            char[8192] meta = void;
            char[256][MAX_ITEMS] files = void;
            int c = 0;
            if (loadInstalledRecord(tok.ptr, meta.ptr, meta.length, files.ptr, files.length, &c) == 0) {
                char[64] ver = void;
                char[64] arch = void;
                char[256] summary = void;
                extractJsonString(meta.ptr, "version".ptr, ver.ptr, ver.length);
                extractJsonString(meta.ptr, "arch".ptr, arch.ptr, arch.length);
                extractJsonString(meta.ptr, "summary".ptr, summary.ptr, summary.length);
                printf("%s %s %s - %s\n", tok.ptr, ver.ptr, arch.ptr, summary.ptr);
                shown++;
            }
        }
    }
    if (shown == 0) printf("No packages installed.\n");
    return 0;
}

private int cmdInfo(const char* pkg) {
    char[8192] meta = void;
    char[256][MAX_ITEMS] files = void;
    int fileCount = 0;
    if (loadInstalledRecord(pkg, meta.ptr, meta.length, files.ptr, files.length, &fileCount) < 0) {
        printf("opkg info: package is not installed: %s\n", pkg);
        return 1;
    }
    char[128] name = void;
    char[64] ver = void;
    char[64] arch = void;
    char[256] summary = void;
    char[256] desc = void;
    char[128] maint = void;
    char[64] section = void;
    extractJsonString(meta.ptr, "name".ptr, name.ptr, name.length);
    extractJsonString(meta.ptr, "version".ptr, ver.ptr, ver.length);
    extractJsonString(meta.ptr, "arch".ptr, arch.ptr, arch.length);
    extractJsonString(meta.ptr, "summary".ptr, summary.ptr, summary.length);
    extractJsonString(meta.ptr, "description".ptr, desc.ptr, desc.length);
    extractJsonString(meta.ptr, "maintainer".ptr, maint.ptr, maint.length);
    extractJsonString(meta.ptr, "section".ptr, section.ptr, section.length);
    printf("Name: %s\n", name.ptr);
    printf("Version: %s\n", ver.ptr);
    printf("Arch: %s\n", arch.ptr);
    printf("Summary: %s\n", summary.ptr);
    printf("Description: %s\n", desc.ptr);
    printf("Maintainer: %s\n", maint.ptr);
    printf("Section: %s\n", section.ptr);
    printf("Installed files: %d\n", fileCount);
    return 0;
}

private int cmdFiles(const char* pkg) {
    char[8192] meta = void;
    char[256][MAX_ITEMS] files = void;
    int fileCount = 0;
    if (loadInstalledRecord(pkg, meta.ptr, meta.length, files.ptr, files.length, &fileCount) < 0) {
        printf("opkg files: package is not installed: %s\n", pkg);
        return 1;
    }
    for (int i = 0; i < fileCount; i++) {
        printf("%s\n", files[i].ptr);
    }
    return 0;
}

private int cmdOwner(const char* pathArg) {
    char[256] path = void;
    copyStr(path.ptr, pathArg, path.length);
    normalizePath(path.ptr);
    OwnerEntry[MAX_ITEMS] owners = void;
    int n = loadOwners("/var/lib/opkg/fileowners.json".ptr, owners.ptr, owners.length);
    for (int i = 0; i < n; i++) {
        if (streq(owners[i].path.ptr, path.ptr)) {
            printf("%s -> %s\n", path.ptr, owners[i].pkg.ptr);
            return 0;
        }
    }
    printf("%s: not owned by any installed package\n", path.ptr);
    return 1;
}

private int cmdUpdate() {
    ensureDbDirs();
    RepoConfigEntry[32] repos = void;
    int nrepos = parseRepoConfig(repos.ptr, repos.length);
    if (nrepos <= 0) {
        printf("opkg update: no repos configured in /etc/opkg/repos.conf\n");
        return 1;
    }
    int ok = 0;
    printf("Refreshing repository indexes...\n");
    for (int i = 0; i < nrepos; i++) {
        if (startsWith(repos[i].base.ptr, "http://".ptr) || startsWith(repos[i].base.ptr, "https://".ptr)) {
            printf("  [fail] %s: http/https transport unavailable in static runtime profile\n", repos[i].name.ptr);
            continue;
        }
        char[256] basePath = void;
        if (startsWith(repos[i].base.ptr, "file://".ptr)) {
            copyStr(basePath.ptr, repos[i].base.ptr + 7, basePath.length);
        } else {
            copyStr(basePath.ptr, repos[i].base.ptr, basePath.length);
        }

        char[320] src = void;
        copyStr(src.ptr, basePath.ptr, src.length);
        appendStr(src.ptr, "/index.json".ptr, src.length);
        char[8192] idx = void;
        if (readFile(src.ptr, idx.ptr, idx.length) < 0) {
            printf("  [fail] %s: cannot read %s\n", repos[i].name.ptr, src.ptr);
            continue;
        }
        char[320] dst = void;
        copyStr(dst.ptr, "/var/lib/opkg/repos/".ptr, dst.length);
        appendStr(dst.ptr, repos[i].name.ptr, dst.length);
        appendStr(dst.ptr, ".json".ptr, dst.length);
        if (writeFile(dst.ptr, idx.ptr) < 0) {
            printf("  [fail] %s: cannot write cache\n", repos[i].name.ptr);
            continue;
        }
        printf("  [ok] %s %s\n", repos[i].name.ptr, repos[i].base.ptr);
        ok++;
    }
    if (ok == 0) {
        printf("opkg update: no repos updated\n");
        return 1;
    }
    printf("opkg update: updated %d repo(s)\n", ok);
    return 0;
}

private int cmdSearch(const char* term) {
    char[1024] lsout = void;
    char*[4] lsArgs = [ cast(char*)"/bin/ls".ptr, cast(char*)"/var/lib/opkg/repos".ptr, null, null ];
    if (runExecCapture("/bin/ls".ptr, lsArgs.ptr, lsout.ptr, lsout.length) != 0) {
        printf("opkg search: no cached repositories, run: opkg update\n");
        return 1;
    }

    printf("Searching cached repositories for: %s\n", term);
    int matches = 0;
    ulong i = 0;
    while (lsout[i] != 0) {
        char[128] file = void;
        ulong j = 0;
        while (lsout[i] == ' ' || lsout[i] == '\n' || lsout[i] == '\t' || lsout[i] == '\r') i++;
        while (lsout[i] != 0 && lsout[i] != ' ' && lsout[i] != '\n' && lsout[i] != '\t' && lsout[i] != '\r' && j + 1 < file.length) file[j++] = lsout[i++];
        file[j] = 0;
        if (j == 0) break;
        if (endsWith(file.ptr, ".json".ptr) == 0) continue;

        char[320] path = void;
        copyStr(path.ptr, "/var/lib/opkg/repos/".ptr, path.length);
        appendStr(path.ptr, file.ptr, path.length);
        char[MAX_FILE] idx = void;
        if (readFile(path.ptr, idx.ptr, idx.length) < 0) continue;

        RepoIndexEntry[256] entries = void;
        int n = parseRepoIndexJson(idx.ptr, entries.ptr, entries.length);
        file[j - 5] = 0;
        for (int e = 0; e < n; e++) {
            if (containsSubstr(entries[e].name.ptr, term) != 0 || containsSubstr(entries[e].summary.ptr, term) != 0) {
                printf("  [%s] %s %s (%s)\n", file.ptr, entries[e].name.ptr, entries[e].ver.ptr, entries[e].arch.ptr);
                if (entries[e].summary[0] != 0) printf("      %s\n", entries[e].summary.ptr);
                matches++;
            }
        }
    }
    if (matches == 0) {
        printf("No matching packages found for: %s\n", term);
        return 1;
    }
    return 0;
}

private int installFromRepoName(const char* pkgName) {
    if (isSafePkgName(pkgName) == 0) {
        printf("opkg install: invalid package name\n");
        return 1;
    }
    RepoConfigEntry[32] repos = void;
    int nrepos = parseRepoConfig(repos.ptr, repos.length);
    if (nrepos <= 0) {
        printf("opkg install: no repos configured\n");
        return 1;
    }

    char[1024] lsout = void;
    char*[4] lsArgs = [ cast(char*)"/bin/ls".ptr, cast(char*)"/var/lib/opkg/repos".ptr, null, null ];
    if (runExecCapture("/bin/ls".ptr, lsArgs.ptr, lsout.ptr, lsout.length) != 0) {
        printf("opkg install: no cached repositories, run: opkg update\n");
        return 1;
    }

    RepoIndexEntry best = void;
    RepoConfigEntry bestRepo = void;
    int found = 0;
    ulong i = 0;
    while (lsout[i] != 0) {
        char[128] file = void;
        ulong j = 0;
        while (lsout[i] == ' ' || lsout[i] == '\n' || lsout[i] == '\t' || lsout[i] == '\r') i++;
        while (lsout[i] != 0 && lsout[i] != ' ' && lsout[i] != '\n' && lsout[i] != '\t' && lsout[i] != '\r' && j + 1 < file.length) file[j++] = lsout[i++];
        file[j] = 0;
        if (j == 0) break;
        if (endsWith(file.ptr, ".json".ptr) == 0) continue;

        file[j - 5] = 0;
        int ridx = -1;
        for (int r = 0; r < nrepos; r++) {
            if (streq(file.ptr, repos[r].name.ptr)) {
                ridx = r;
                break;
            }
        }
        if (ridx < 0) continue;

        char[320] cachePath = void;
        copyStr(cachePath.ptr, "/var/lib/opkg/repos/".ptr, cachePath.length);
        appendStr(cachePath.ptr, repos[ridx].name.ptr, cachePath.length);
        appendStr(cachePath.ptr, ".json".ptr, cachePath.length);
        char[MAX_FILE] idx = void;
        if (readFile(cachePath.ptr, idx.ptr, idx.length) < 0) continue;
        RepoIndexEntry[256] entries = void;
        int n = parseRepoIndexJson(idx.ptr, entries.ptr, entries.length);
        for (int e = 0; e < n; e++) {
            if (streq(entries[e].name.ptr, pkgName) == 0) continue;
            if (streq(entries[e].arch.ptr, "x86_64".ptr) == 0) continue;
            if (found == 0 || versionCmp(best.ver.ptr, entries[e].ver.ptr) < 0) {
                best = entries[e];
                bestRepo = repos[ridx];
                found = 1;
            }
        }
    }

    if (found == 0) {
        printf("opkg install: package not found: %s\n", pkgName);
        return 1;
    }

    if (startsWith(bestRepo.base.ptr, "http://".ptr) || startsWith(bestRepo.base.ptr, "https://".ptr)) {
        printf("opkg install: repo install over http/https not available in static runtime profile yet\n");
        return 1;
    }

    char[256] basePath = void;
    if (startsWith(bestRepo.base.ptr, "file://".ptr)) {
        copyStr(basePath.ptr, bestRepo.base.ptr + 7, basePath.length);
    } else {
        copyStr(basePath.ptr, bestRepo.base.ptr, basePath.length);
    }
    char[512] pkgPath = void;
    copyStr(pkgPath.ptr, basePath.ptr, pkgPath.length);
    appendStr(pkgPath.ptr, "/packages/".ptr, pkgPath.length);
    appendStr(pkgPath.ptr, best.filename.ptr, pkgPath.length);

    printf("==> Resolving %s from repo %s\n", pkgName, bestRepo.name.ptr);
    printf("==> Installing %s\n", pkgPath.ptr);
    return installLocal(pkgPath.ptr);
}

private int cmdRepo() {
    RepoConfigEntry[32] repos = void;
    int n = parseRepoConfig(repos.ptr, repos.length);
    if (n <= 0) {
        printf("No repositories configured in /etc/opkg/repos.conf\n");
        return 1;
    }
    printf("Configured repositories:\n");
    for (int i = 0; i < n; i++) {
        printf("  - %s => %s\n", repos[i].name.ptr, repos[i].base.ptr);
    }
    return 0;
}

private int cmdDoctor() {
    printf("opkg doctor\n");
    printf("  runtime: static D profile\n");
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        close(s);
        printf("  udp socket syscall: available\n");
        printf("  note: tcp/http transport is still not implemented in this profile\n");
    } else {
        printf("  udp socket syscall: unavailable (%d)\n", s);
        printf("  web repo in-OS usage: not available until transport support is complete\n");
    }

    char[64] cfg = void;
    if (readFile("/etc/opkg/repos.conf".ptr, cfg.ptr, cfg.length) < 0) {
        printf("  repos.conf: missing (/etc/opkg/repos.conf)\n");
    } else {
        printf("  repos.conf: present\n");
    }
    return 0;
}

private int cmdBuild(const char* dir) {
    cast(void)dir;
    printf("opkg build: not available in this static runtime profile yet\n");
    return 0;
}

private void printUsage() {
    printf("Usage: opkg <command> [args]\n\n");
    printf("Commands:\n");
    printf("  install <pkg|file.opk> Install package from repo cache or local file\n");
    printf("  remove <pkg>           Remove installed package\n");
    printf("  list                   List installed packages\n");
    printf("  info <pkg>             Show package metadata\n");
    printf("  files <pkg>            List files installed by package\n");
    printf("  owner <path>           Show owning package\n");
    printf("  build <dir>            Not available in static profile yet\n");
    printf("  update                 Refresh local repo cache (file:// or local path)\n");
    printf("  search <term>          Search cached repository metadata\n");
    printf("  repo                   List configured repositories\n");
    printf("  doctor                 Show runtime capability diagnostics\n");
}

extern(C) int opkg_main_d(int argc, char** argv) {
    if (argv is null || argc <= 1) {
        printUsage();
        return 0;
    }
    auto cmd = argv[1];
    if (streq(cmd, "-h".ptr) || streq(cmd, "--help".ptr) || streq(cmd, "help".ptr)) {
        printUsage();
        return 0;
    }
    if (streq(cmd, "install".ptr)) {
        if (argc < 3) {
            printf("usage: opkg install <pkg|file.opk>\n");
            return 1;
        }
        if (endsWith(argv[2], ".opk".ptr) != 0) return installLocal(argv[2]);
        return installFromRepoName(argv[2]);
    }
    if (streq(cmd, "remove".ptr)) {
        if (argc < 3) {
            printf("usage: opkg remove <pkg>\n");
            return 1;
        }
        return cmdRemove(argv[2]);
    }
    if (streq(cmd, "list".ptr)) return cmdList();
    if (streq(cmd, "info".ptr)) {
        if (argc < 3) {
            printf("usage: opkg info <pkg>\n");
            return 1;
        }
        return cmdInfo(argv[2]);
    }
    if (streq(cmd, "files".ptr)) {
        if (argc < 3) {
            printf("usage: opkg files <pkg>\n");
            return 1;
        }
        return cmdFiles(argv[2]);
    }
    if (streq(cmd, "owner".ptr)) {
        if (argc < 3) {
            printf("usage: opkg owner <path>\n");
            return 1;
        }
        return cmdOwner(argv[2]);
    }
    if (streq(cmd, "build".ptr)) {
        if (argc < 3) {
            printf("usage: opkg build <dir>\n");
            return 1;
        }
        return cmdBuild(argv[2]);
    }
    if (streq(cmd, "update".ptr)) return cmdUpdate();
    if (streq(cmd, "search".ptr)) {
        if (argc < 3) {
            printf("usage: opkg search <term>\n");
            return 1;
        }
        return cmdSearch(argv[2]);
    }
    if (streq(cmd, "repo".ptr)) return cmdRepo();
    if (streq(cmd, "doctor".ptr)) return cmdDoctor();
    printf("opkg: unknown command: %s\n", cmd);
    return 1;
}
