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
    char[96] indexChecksum;
}

struct RepoIndexEntry {
    char[128] name;
    char[64] ver;
    char[64] arch;
    char[256] filename;
    char[96] checksum;
    char[64][16] depends;
    int depCount;
    char[256] summary;
}

struct RecipeManifest {
    char[64] name;
    char[64] ver;
    char[64] arch;
    char[64] section;
    char[96] license;
    char[256] summary;
    char[512] description;
    char[256] source;
    char[64] build;
    char[64][16] depends;
    int depCount;
}

struct Sha256Ctx {
    uint[8] state;
    ulong bitlen;
    uint datalen;
    ubyte[64] data;
}

private uint rotr32(uint x, uint n) {
    return (x >> n) | (x << (32 - n));
}

private void sha256Transform(ref Sha256Ctx ctx, const(ubyte)* block) {
    enum uint[64] K = [
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    ];
    uint[64] m = void;
    for (int i = 0; i < 16; i++) {
        int j = i * 4;
        m[i] = (cast(uint)block[j] << 24) |
               (cast(uint)block[j + 1] << 16) |
               (cast(uint)block[j + 2] << 8) |
               cast(uint)block[j + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint s0 = rotr32(m[i - 15], 7) ^ rotr32(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint s1 = rotr32(m[i - 2], 17) ^ rotr32(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint a = ctx.state[0];
    uint b = ctx.state[1];
    uint c = ctx.state[2];
    uint d = ctx.state[3];
    uint e = ctx.state[4];
    uint f = ctx.state[5];
    uint g = ctx.state[6];
    uint h = ctx.state[7];

    for (int i = 0; i < 64; i++) {
        uint S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint ch = (e & f) ^ ((~e) & g);
        uint t1 = h + S1 + ch + K[i] + m[i];
        uint S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint maj = (a & b) ^ (a & c) ^ (b & c);
        uint t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

private void sha256Init(ref Sha256Ctx ctx) {
    ctx.datalen = 0;
    ctx.bitlen = 0;
    ctx.state[0] = 0x6a09e667U;
    ctx.state[1] = 0xbb67ae85U;
    ctx.state[2] = 0x3c6ef372U;
    ctx.state[3] = 0xa54ff53aU;
    ctx.state[4] = 0x510e527fU;
    ctx.state[5] = 0x9b05688cU;
    ctx.state[6] = 0x1f83d9abU;
    ctx.state[7] = 0x5be0cd19U;
}

private void sha256Update(ref Sha256Ctx ctx, const(ubyte)* data, ulong len) {
    for (ulong i = 0; i < len; i++) {
        ctx.data[ctx.datalen++] = data[i];
        if (ctx.datalen == 64) {
            sha256Transform(ctx, ctx.data.ptr);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }
}

private void sha256Final(ref Sha256Ctx ctx, ubyte* out32) {
    uint i = ctx.datalen;
    if (ctx.datalen < 56) {
        ctx.data[i++] = 0x80;
        while (i < 56) ctx.data[i++] = 0;
    } else {
        ctx.data[i++] = 0x80;
        while (i < 64) ctx.data[i++] = 0;
        sha256Transform(ctx, ctx.data.ptr);
        for (i = 0; i < 56; i++) ctx.data[i] = 0;
    }
    ctx.bitlen += cast(ulong)ctx.datalen * 8UL;
    ctx.data[63] = cast(ubyte)(ctx.bitlen);
    ctx.data[62] = cast(ubyte)(ctx.bitlen >> 8);
    ctx.data[61] = cast(ubyte)(ctx.bitlen >> 16);
    ctx.data[60] = cast(ubyte)(ctx.bitlen >> 24);
    ctx.data[59] = cast(ubyte)(ctx.bitlen >> 32);
    ctx.data[58] = cast(ubyte)(ctx.bitlen >> 40);
    ctx.data[57] = cast(ubyte)(ctx.bitlen >> 48);
    ctx.data[56] = cast(ubyte)(ctx.bitlen >> 56);
    sha256Transform(ctx, ctx.data.ptr);

    for (i = 0; i < 4; i++) {
        out32[i]      = cast(ubyte)(ctx.state[0] >> (24 - i * 8));
        out32[i + 4]  = cast(ubyte)(ctx.state[1] >> (24 - i * 8));
        out32[i + 8]  = cast(ubyte)(ctx.state[2] >> (24 - i * 8));
        out32[i + 12] = cast(ubyte)(ctx.state[3] >> (24 - i * 8));
        out32[i + 16] = cast(ubyte)(ctx.state[4] >> (24 - i * 8));
        out32[i + 20] = cast(ubyte)(ctx.state[5] >> (24 - i * 8));
        out32[i + 24] = cast(ubyte)(ctx.state[6] >> (24 - i * 8));
        out32[i + 28] = cast(ubyte)(ctx.state[7] >> (24 - i * 8));
    }
}

private int parseChecksumSha256(const char* field, char* outHex, ulong cap) {
    if (!field || !outHex || cap < 65) return -1;
    if (!(field[0] == 's' || field[0] == 'S') ||
        !(field[1] == 'h' || field[1] == 'H') ||
        !(field[2] == 'a' || field[2] == 'A') ||
        field[3] != '2' || field[4] != '5' || field[5] != '6' || field[6] != ':') {
        return -1;
    }
    for (int i = 0; i < 64; i++) {
        char c = field[7 + i];
        if (c == 0) return -1;
        if (c >= 'A' && c <= 'F') c = cast(char)(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return -1;
        outHex[i] = c;
    }
    if (field[71] != 0) return -1;
    outHex[64] = 0;
    return 0;
}

private int hashFileSha256Hex(const char* path, char* outHex, ulong cap) {
    int fd;
    Sha256Ctx ctx = void;
    ubyte[32] dig = void;
    ubyte[1024] buf = void;
    if (!path || !outHex || cap < 65) return -1;
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    sha256Init(ctx);
    while (true) {
        auto n = read(fd, buf.ptr, buf.length);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) break;
        sha256Update(ctx, buf.ptr, cast(ulong)n);
    }
    close(fd);
    sha256Final(ctx, dig.ptr);
    for (int i = 0; i < 32; i++) {
        ubyte b = dig[i];
        outHex[i * 2] = cast(char)((b >> 4) < 10 ? ('0' + (b >> 4)) : ('a' + ((b >> 4) - 10)));
        outHex[i * 2 + 1] = cast(char)((b & 0x0F) < 10 ? ('0' + (b & 0x0F)) : ('a' + ((b & 0x0F) - 10)));
    }
    outHex[64] = 0;
    return 0;
}

private int verifyRepoPackageChecksum(const char* pkgPath, const char* checksumField) {
    char[65] expected = void;
    char[65] got = void;
    if (parseChecksumSha256(checksumField, expected.ptr, expected.length) < 0) {
        printf("opkg install: repository checksum is missing/invalid for package\n");
        return -1;
    }
    if (hashFileSha256Hex(pkgPath, got.ptr, got.length) < 0) {
        printf("opkg install: failed to hash downloaded package for verification\n");
        return -1;
    }
    if (streq(got.ptr, expected.ptr) == 0) {
        printf("opkg install: checksum mismatch\n");
        printf("  expected: sha256:%s\n", expected.ptr);
        printf("  got:      sha256:%s\n", got.ptr);
        return -1;
    }
    return 0;
}

private int verifyRepoIndexChecksum(const char* indexPath, const char* checksumField) {
    if (!checksumField || checksumField[0] == 0) {
        return 0;
    }
    char[65] expected = void;
    char[65] got = void;
    if (parseChecksumSha256(checksumField, expected.ptr, expected.length) < 0) {
        printf("opkg update: invalid index checksum pin in repos.conf\n");
        return -1;
    }
    if (hashFileSha256Hex(indexPath, got.ptr, got.length) < 0) {
        printf("opkg update: failed to hash repo index for verification\n");
        return -1;
    }
    if (streq(got.ptr, expected.ptr) == 0) {
        printf("opkg update: index checksum mismatch\n");
        printf("  expected: sha256:%s\n", expected.ptr);
        printf("  got:      sha256:%s\n", got.ptr);
        return -1;
    }
    return 0;
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
    /* Some opk toolchains omit the final 0-block(s) from the embedded files.tar,
     * relying on the outer tar's padding to align the next member. We mirror
     * that outer padding into filesTarOut so our inner tar parser can reliably
     * detect end-of-archive. */
    ubyte[512] zeroBlock = void;
    int gotMeta = 0;
    int gotFilesTar = 0;
    metaOut[0] = 0;
    int lastWasFilesTar = 0;

    while (true) {
        lastWasFilesTar = 0;
        if (readExact(fd, hdr.ptr, TAR_BLOCK) < 0) {
            close(outfd);
            close(fd);
            return -1;
        }
        if (isZeroBlock(hdr.ptr) != 0) break;

        char[256] name = void;
        copyTarName(name.ptr, name.length, hdr.ptr);
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
            lastWasFilesTar = 1;
        } else {
            // Accept tar metadata members (e.g. PAX headers) and ignore
            // any non-opkg payload entries rather than rejecting package.
            if (skipBytes(fd, size) < 0) {
                close(outfd);
                close(fd);
                return -1;
            }
        }

        ulong pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
        if (pad > 0) {
            if (lastWasFilesTar) {
                if (writeExact(outfd, zeroBlock.ptr, pad) < 0) {
                    close(outfd);
                    close(fd);
                    return -1;
                }
            }
            if (skipBytes(fd, pad) < 0) {
            close(outfd);
            close(fd);
            return -1;
            }
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

private int fileExists(const char* path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

private int pickCaBundle(char* outPath, ulong outCap) {
    if (fileExists("/etc/ssl/certs/ca-certificates.crt".ptr) != 0) {
        copyStr(outPath, "/etc/ssl/certs/ca-certificates.crt".ptr, outCap);
        return 0;
    }
    if (fileExists("/etc/ssl/cert.pem".ptr) != 0) {
        copyStr(outPath, "/etc/ssl/cert.pem".ptr, outCap);
        return 0;
    }
    if (fileExists("/etc/ssl/certs.pem".ptr) != 0) {
        copyStr(outPath, "/etc/ssl/certs.pem".ptr, outCap);
        return 0;
    }
    outPath[0] = 0;
    return -1;
}

private int toolSupportsHttps(const char* tool) {
    char[256] probeOut = void;
    if (endsWith(tool, "curl".ptr) != 0) {
        char*[5] argv = [ cast(char*)"curl".ptr, cast(char*)"--version".ptr, null, null, null ];
        if (runExecCapture(tool, argv.ptr, probeOut.ptr, probeOut.length) != 0) {
            return 0;
        }
        if (containsSubstr(probeOut.ptr, "https".ptr) != 0 || containsSubstr(probeOut.ptr, "HTTPS".ptr) != 0) {
            return 1;
        }
        return 0;
    }
    /* Assume external wget/fetch may handle HTTPS with cert verification. */
    if (endsWith(tool, "wget".ptr) != 0 || endsWith(tool, "fetch".ptr) != 0) {
        return 1;
    }
    return 0;
}

private int selectFetchTool(char* outPath, ulong outCap) {
    if (fileExists("/bin/fetch".ptr) != 0) {
        copyStr(outPath, "/bin/fetch".ptr, outCap);
        return 0;
    }
    if (fileExists("/usr/bin/fetch".ptr) != 0) {
        copyStr(outPath, "/usr/bin/fetch".ptr, outCap);
        return 0;
    }
    if (fileExists("/bin/curl".ptr) != 0) {
        copyStr(outPath, "/bin/curl".ptr, outCap);
        return 0;
    }
    if (fileExists("/usr/bin/curl".ptr) != 0) {
        copyStr(outPath, "/usr/bin/curl".ptr, outCap);
        return 0;
    }
    if (fileExists("/bin/wget".ptr) != 0) {
        copyStr(outPath, "/bin/wget".ptr, outCap);
        return 0;
    }
    if (fileExists("/usr/bin/wget".ptr) != 0) {
        copyStr(outPath, "/usr/bin/wget".ptr, outCap);
        return 0;
    }
    outPath[0] = 0;
    return -1;
}

private int selectHttpsFetchTool(char* outPath, ulong outCap) {
    if (fileExists("/bin/fetch".ptr) != 0 && toolSupportsHttps("/bin/fetch".ptr) != 0) {
        copyStr(outPath, "/bin/fetch".ptr, outCap);
        return 0;
    }
    if (fileExists("/usr/bin/fetch".ptr) != 0 && toolSupportsHttps("/usr/bin/fetch".ptr) != 0) {
        copyStr(outPath, "/usr/bin/fetch".ptr, outCap);
        return 0;
    }
    if (fileExists("/bin/curl".ptr) != 0 && toolSupportsHttps("/bin/curl".ptr) != 0) {
        copyStr(outPath, "/bin/curl".ptr, outCap);
        return 0;
    }
    if (fileExists("/usr/bin/curl".ptr) != 0 && toolSupportsHttps("/usr/bin/curl".ptr) != 0) {
        copyStr(outPath, "/usr/bin/curl".ptr, outCap);
        return 0;
    }
    if (fileExists("/bin/wget".ptr) != 0 && toolSupportsHttps("/bin/wget".ptr) != 0) {
        copyStr(outPath, "/bin/wget".ptr, outCap);
        return 0;
    }
    if (fileExists("/usr/bin/wget".ptr) != 0 && toolSupportsHttps("/usr/bin/wget".ptr) != 0) {
        copyStr(outPath, "/usr/bin/wget".ptr, outCap);
        return 0;
    }
    outPath[0] = 0;
    return -1;
}

private int fetchHttpToFile(const char* url, const char* dst) {
    char[64] tool = void;
    char[96] ca = void;
    int isHttps = startsWith(url, "https://".ptr) != 0;
    if (isHttps) {
        if (selectHttpsFetchTool(tool.ptr, tool.length) < 0) {
            if (selectFetchTool(tool.ptr, tool.length) == 0) {
                return -2;
            }
            return -1;
        }
        if (toolSupportsHttps(tool.ptr) == 0) {
            return -2;
        }
        if (pickCaBundle(ca.ptr, ca.length) < 0) {
            return -3;
        }
    } else {
        if (selectFetchTool(tool.ptr, tool.length) < 0) {
            return -1;
        }
    }

    if (endsWith(tool.ptr, "curl".ptr) != 0) {
        if (isHttps) {
            char*[12] argvTls = [
                cast(char*)"curl".ptr,
                cast(char*)"-f".ptr,
                cast(char*)"-s".ptr,
                cast(char*)"-S".ptr,
                cast(char*)"--cacert".ptr,
                cast(char*)ca.ptr,
                cast(char*)"-o".ptr,
                cast(char*)dst,
                cast(char*)url,
                null, null, null
            ];
            return runExec(tool.ptr, argvTls.ptr);
        }
        char*[12] argv = [
            cast(char*)"curl".ptr,
            cast(char*)"-f".ptr,
            cast(char*)"-s".ptr,
            cast(char*)"-S".ptr,
            cast(char*)"-o".ptr,
            cast(char*)dst,
            cast(char*)url,
            null, null, null, null, null
        ];
        return runExec(tool.ptr, argv.ptr);
    }
    if (endsWith(tool.ptr, "wget".ptr) != 0) {
        if (isHttps) {
            char*[12] argvTls = [
                cast(char*)"wget".ptr,
                cast(char*)"-q".ptr,
                cast(char*)"--https-only".ptr,
                cast(char*)"--ca-certificate".ptr,
                cast(char*)ca.ptr,
                cast(char*)"-O".ptr,
                cast(char*)dst,
                cast(char*)url,
                null, null, null, null
            ];
            return runExec(tool.ptr, argvTls.ptr);
        }
        char*[12] argv = [
            cast(char*)"wget".ptr,
            cast(char*)"-q".ptr,
            cast(char*)"-O".ptr,
            cast(char*)dst,
            cast(char*)url,
            null, null, null, null, null, null, null
        ];
        return runExec(tool.ptr, argv.ptr);
    }
    /* BSD-style fetch */
    if (isHttps) {
        char*[12] argvTls = [
            cast(char*)"fetch".ptr,
            cast(char*)"--ca-cert".ptr,
            cast(char*)ca.ptr,
            cast(char*)"-o".ptr,
            cast(char*)dst,
            cast(char*)url,
            null, null, null, null, null, null
        ];
        return runExec(tool.ptr, argvTls.ptr);
    }
    char*[12] argv = [
        cast(char*)"fetch".ptr,
        cast(char*)"-o".ptr,
        cast(char*)dst,
        cast(char*)url,
        null, null, null, null, null, null, null, null
    ];
    return runExec(tool.ptr, argv.ptr);
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

private int parseDependencyName(const char* raw, char* outName, ulong cap) {
    ulong i = 0;
    ulong j = 0;
    if (!raw || !outName || cap == 0) return -1;
    while (raw[i] == ' ' || raw[i] == '\t') i++;
    while (raw[i] != 0 && j + 1 < cap) {
        char c = raw[i];
        int ok = (c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 c == '-' || c == '_' || c == '.' || c == '+';
        if (!ok) break;
        outName[j++] = c;
        i++;
    }
    outName[j] = 0;
    return j > 0 ? 0 : -1;
}

private int depNameInStack(const char* name, char[64]* stack, int stackCount) {
    for (int i = 0; i < stackCount; i++) {
        if (streq(stack[i].ptr, name)) return 1;
    }
    return 0;
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
    mkdir("/var/cache".ptr, 493);
    /* Allow non-root users to write opkg runtime cache/index files. */
    mkdir("/var/cache/opkg".ptr, 511);
    mkdir("/var/lib".ptr, 493);
    mkdir("/var/lib/opkg".ptr, 511);
    mkdir("/var/lib/opkg/installed".ptr, 511);
    mkdir("/var/lib/opkg/repos".ptr, 511);
    mkdir("/var/lib/opkg/cache".ptr, 511);
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
        char[96] indexChecksum = void;
        ulong n = 0;
        while (text[i] != 0 && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && n + 1 < name.length) {
            name[n++] = text[i++];
        }
        name[n] = 0;
        while (text[i] == ' ' || text[i] == '\t') i++;
        ulong b = 0;
        while (text[i] != 0 && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && text[i] != '#' && b + 1 < base.length) {
            base[b++] = text[i++];
        }
        base[b] = 0;
        while (text[i] == ' ' || text[i] == '\t') i++;
        ulong c = 0;
        while (text[i] != 0 && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && text[i] != '#' && c + 1 < indexChecksum.length) {
            indexChecksum[c++] = text[i++];
        }
        indexChecksum[c] = 0;
        while (text[i] != 0 && text[i] != '\n') i++;
        while (text[i] == '\r' || text[i] == '\n') i++;

        if (name[0] == 0 || base[0] == 0) continue;
        copyStr(repos[count].name.ptr, name.ptr, repos[count].name.length);
        copyStr(repos[count].base.ptr, base.ptr, repos[count].base.length);
        copyStr(repos[count].indexChecksum.ptr, indexChecksum.ptr, repos[count].indexChecksum.length);
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
        {
            char[256][16] deps = void;
            int dn = extractJsonArrayStrings(obj.ptr, "depends".ptr, deps.ptr, deps.length);
            outEntries[count].depCount = 0;
            for (int d = 0; d < dn && d < outEntries[count].depends.length; d++) {
                char[64] depName = void;
                if (parseDependencyName(deps[d].ptr, depName.ptr, depName.length) == 0) {
                    copyStr(outEntries[count].depends[outEntries[count].depCount].ptr,
                            depName.ptr,
                            outEntries[count].depends[outEntries[count].depCount].length);
                    outEntries[count].depCount++;
                }
            }
        }
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
    copyStr(filesTar.ptr, "/var/cache/opkg/opkg-files.tar".ptr, filesTar.length);
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
    {
        char[256][32] deps = void;
        int depCount = extractJsonArrayStrings(meta.ptr, "depends".ptr, deps.ptr, deps.length);
        for (int i = 0; i < depCount; i++) {
            char[64] depName = void;
            if (parseDependencyName(deps[i].ptr, depName.ptr, depName.length) < 0) continue;
            if (streq(depName.ptr, pkg.ptr)) continue;
            if (isInstalled(depName.ptr) == 0) {
                printf("opkg install: missing dependency: %s (required by %s)\n", depName.ptr, pkg.ptr);
                return 1;
            }
        }
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
    RepoConfigEntry[32] repos = void;
    int nrepos = parseRepoConfig(repos.ptr, repos.length);
    int shown = 0;
    for (int r = 0; r < nrepos; r++) {
        char[320] path = void;
        copyStr(path.ptr, "/var/lib/opkg/repos/".ptr, path.length);
        appendStr(path.ptr, repos[r].name.ptr, path.length);
        appendStr(path.ptr, ".json".ptr, path.length);
        char[MAX_FILE] idx = void;
        if (readFile(path.ptr, idx.ptr, idx.length) < 0) continue;
        RepoIndexEntry[256] entries = void;
        int n = parseRepoIndexJson(idx.ptr, entries.ptr, entries.length);
        for (int e = 0; e < n; e++) {
            if (isInstalled(entries[e].name.ptr) == 0) continue;
            char[8192] meta = void;
            char[256][MAX_ITEMS] files = void;
            int c = 0;
            if (loadInstalledRecord(entries[e].name.ptr, meta.ptr, meta.length, files.ptr, files.length, &c) == 0) {
                char[64] ver = void;
                char[64] arch = void;
                char[256] summary = void;
                extractJsonString(meta.ptr, "version".ptr, ver.ptr, ver.length);
                extractJsonString(meta.ptr, "arch".ptr, arch.ptr, arch.length);
                extractJsonString(meta.ptr, "summary".ptr, summary.ptr, summary.length);
                printf("%s %s %s - %s\n", entries[e].name.ptr, ver.ptr, arch.ptr, summary.ptr);
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
        char[320] dst = void;
        copyStr(dst.ptr, "/var/lib/opkg/repos/".ptr, dst.length);
        appendStr(dst.ptr, repos[i].name.ptr, dst.length);
        appendStr(dst.ptr, ".json".ptr, dst.length);

        if (startsWith(repos[i].base.ptr, "http://".ptr) || startsWith(repos[i].base.ptr, "https://".ptr)) {
            char[320] url = void;
            copyStr(url.ptr, repos[i].base.ptr, url.length);
            appendStr(url.ptr, "/index.json".ptr, url.length);
            int fr = fetchHttpToFile(url.ptr, dst.ptr);
            if (fr < 0) {
                if (fr == -2) {
                    printf("  [fail] %s: https requires TLS-capable fetch helper\n", repos[i].name.ptr);
                } else if (fr == -3) {
                    printf("  [fail] %s: https requires CA bundle (/etc/ssl/certs/ca-certificates.crt)\n", repos[i].name.ptr);
                } else {
                    printf("  [fail] %s: web fetch failed (need curl/wget/fetch + TCP)\n", repos[i].name.ptr);
                }
                continue;
            }
        } else {
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
            if (writeFile(dst.ptr, idx.ptr) < 0) {
                printf("  [fail] %s: cannot write cache\n", repos[i].name.ptr);
                continue;
            }
        }
        if (verifyRepoIndexChecksum(dst.ptr, repos[i].indexChecksum.ptr) < 0) {
            printf("  [fail] %s: index verification failed\n", repos[i].name.ptr);
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
    RepoConfigEntry[32] repos = void;
    int nrepos = parseRepoConfig(repos.ptr, repos.length);
    if (nrepos <= 0) {
        printf("opkg search: no repositories configured\n");
        return 1;
    }
    printf("Searching cached repositories for: %s\n", term);
    int matches = 0;
    for (int r = 0; r < nrepos; r++) {
        char[320] path = void;
        copyStr(path.ptr, "/var/lib/opkg/repos/".ptr, path.length);
        appendStr(path.ptr, repos[r].name.ptr, path.length);
        appendStr(path.ptr, ".json".ptr, path.length);
        char[MAX_FILE] idx = void;
        if (readFile(path.ptr, idx.ptr, idx.length) < 0) {
            continue;
        }

        RepoIndexEntry[256] entries = void;
        int n = parseRepoIndexJson(idx.ptr, entries.ptr, entries.length);
        for (int e = 0; e < n; e++) {
            if (containsSubstr(entries[e].name.ptr, term) != 0 || containsSubstr(entries[e].summary.ptr, term) != 0) {
                printf("  [%s] %s %s (%s)\n", repos[r].name.ptr, entries[e].name.ptr, entries[e].ver.ptr, entries[e].arch.ptr);
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

private int findBestRepoPackage(const char* pkgName, RepoIndexEntry* outEntry, RepoConfigEntry* outRepo) {
    RepoConfigEntry[32] repos = void;
    int nrepos = parseRepoConfig(repos.ptr, repos.length);
    if (nrepos <= 0) return -1;

    int found = 0;
    int hadCache = 0;
    for (int pass = 0; pass < 2; pass++) {
        found = 0;
        hadCache = 0;
        for (int ridx = 0; ridx < nrepos; ridx++) {
            char[320] cachePath = void;
            copyStr(cachePath.ptr, "/var/lib/opkg/repos/".ptr, cachePath.length);
            appendStr(cachePath.ptr, repos[ridx].name.ptr, cachePath.length);
            appendStr(cachePath.ptr, ".json".ptr, cachePath.length);
            char[MAX_FILE] idx = void;
            if (readFile(cachePath.ptr, idx.ptr, idx.length) < 0) {
                continue;
            }
            hadCache = 1;
            RepoIndexEntry[256] entries = void;
            int n = parseRepoIndexJson(idx.ptr, entries.ptr, entries.length);
            for (int e = 0; e < n; e++) {
                if (streq(entries[e].name.ptr, pkgName) == 0) continue;
                if (streq(entries[e].arch.ptr, "x86_64".ptr) == 0 && streq(entries[e].arch.ptr, "noarch".ptr) == 0) continue;
                if (found == 0 || versionCmp(outEntry.ver.ptr, entries[e].ver.ptr) < 0) {
                    *outEntry = entries[e];
                    *outRepo = repos[ridx];
                    found = 1;
                }
            }
        }
        if (found != 0) return 0;
        if (pass == 0 && hadCache == 0) {
            /* Lazy refresh so install/install-profile works out-of-box on first boot. */
            cast(void)cmdUpdate();
            continue;
        }
        break;
    }
    return -1;
}

private int installFetchedRepoEntry(RepoIndexEntry* ent, RepoConfigEntry* repo) {
    if (startsWith(repo.base.ptr, "http://".ptr) || startsWith(repo.base.ptr, "https://".ptr)) {
        ensureDbDirs();
        char[512] url = void;
        copyStr(url.ptr, repo.base.ptr, url.length);
        appendStr(url.ptr, "/packages/".ptr, url.length);
        appendStr(url.ptr, ent.filename.ptr, url.length);

        char[512] cachePath = void;
        copyStr(cachePath.ptr, "/var/lib/opkg/cache/".ptr, cachePath.length);
        appendStr(cachePath.ptr, ent.filename.ptr, cachePath.length);
        printf("==> Fetching %s\n", url.ptr);
        int fr = fetchHttpToFile(url.ptr, cachePath.ptr);
        if (fr < 0) {
            if (fr == -2) {
                    printf("opkg install: https requires TLS-capable fetch helper\n");
            } else if (fr == -3) {
                printf("opkg install: https requires CA bundle (/etc/ssl/certs/ca-certificates.crt)\n");
            } else {
                printf("opkg install: web fetch failed (need curl/wget/fetch + TCP)\n");
            }
            return -1;
        }
        if (verifyRepoPackageChecksum(cachePath.ptr, ent.checksum.ptr) < 0) return -1;
        printf("==> Installing %s\n", cachePath.ptr);
        return installLocal(cachePath.ptr) == 0 ? 0 : -1;
    }

    char[256] basePath = void;
    if (startsWith(repo.base.ptr, "file://".ptr)) {
        copyStr(basePath.ptr, repo.base.ptr + 7, basePath.length);
    } else {
        copyStr(basePath.ptr, repo.base.ptr, basePath.length);
    }
    char[512] pkgPath = void;
    copyStr(pkgPath.ptr, basePath.ptr, pkgPath.length);
    appendStr(pkgPath.ptr, "/packages/".ptr, pkgPath.length);
    appendStr(pkgPath.ptr, ent.filename.ptr, pkgPath.length);
    if (verifyRepoPackageChecksum(pkgPath.ptr, ent.checksum.ptr) < 0) return -1;
    printf("==> Installing %s\n", pkgPath.ptr);
    return installLocal(pkgPath.ptr) == 0 ? 0 : -1;
}

private int installFromRepoNameInternal(const char* pkgName, char[64]* depStack, int stackCount) {
    RepoIndexEntry best = void;
    RepoConfigEntry bestRepo = void;

    if (stackCount >= 32) {
        printf("opkg install: dependency depth exceeded while resolving %s\n", pkgName);
        return -1;
    }
    if (depNameInStack(pkgName, depStack, stackCount) != 0) {
        printf("opkg install: dependency cycle detected at %s\n", pkgName);
        return -1;
    }
    if (isInstalled(pkgName) != 0) {
        return 0;
    }
    if (findBestRepoPackage(pkgName, &best, &bestRepo) < 0) {
        printf("opkg install: package not found: %s\n", pkgName);
        return -1;
    }

    copyStr(depStack[stackCount].ptr, pkgName, depStack[stackCount].length);
    stackCount++;

    for (int d = 0; d < best.depCount; d++) {
        char[64] depName = void;
        if (parseDependencyName(best.depends[d].ptr, depName.ptr, depName.length) < 0) continue;
        if (streq(depName.ptr, pkgName)) continue;
        if (isInstalled(depName.ptr) != 0) continue;
        printf("==> Resolving dependency %s for %s\n", depName.ptr, pkgName);
        if (installFromRepoNameInternal(depName.ptr, depStack, stackCount) < 0) {
            return -1;
        }
    }

    printf("==> Resolving %s from repo %s\n", pkgName, bestRepo.name.ptr);
    return installFetchedRepoEntry(&best, &bestRepo);
}

private int installFromRepoName(const char* pkgName) {
    char[64][32] depStack = void;
    if (isSafePkgName(pkgName) == 0) {
        printf("opkg install: invalid package name\n");
        return 1;
    }
    if (installFromRepoNameInternal(pkgName, depStack.ptr, 0) < 0) {
        return 1;
    }
    return 0;
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
        if (repos[i].indexChecksum[0] != 0) {
            printf("  - %s => %s (index pin: %s)\n", repos[i].name.ptr, repos[i].base.ptr, repos[i].indexChecksum.ptr);
        } else {
            printf("  - %s => %s\n", repos[i].name.ptr, repos[i].base.ptr);
        }
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
    } else {
        printf("  udp socket syscall: unavailable (%d)\n", s);
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
        close(s);
        printf("  tcp stream socket syscall: available\n");
    } else {
        printf("  tcp stream socket syscall: unavailable (%d)\n", s);
    }
    char[64] tool = void;
    if (selectFetchTool(tool.ptr, tool.length) == 0) {
        printf("  http fetch tool: %s\n", tool.ptr);
        if (toolSupportsHttps(tool.ptr) != 0) {
            char[96] ca = void;
            if (pickCaBundle(ca.ptr, ca.length) == 0) {
                printf("  https fetch: helper+CA ready (%s)\n", ca.ptr);
            } else {
                printf("  https fetch: helper present but CA bundle missing\n");
            }
        } else {
            printf("  https fetch: tool lacks HTTPS support\n");
        }
    } else {
        printf("  http fetch tool: missing (curl/wget/fetch)\n");
        printf("  https fetch: unavailable (missing helper)\n");
    }
    printf("  web repos: supported when TCP + fetch helper are available\n");

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

private int isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

private char* trimInPlace(char* s) {
    ulong n;
    if (s is null) return s;
    while (*s != 0 && isSpace(*s) != 0) s++;
    n = cstrlen(s);
    while (n > 0 && isSpace(s[n - 1]) != 0) {
        s[n - 1] = 0;
        n--;
    }
    return s;
}

private void recipeDefaults(RecipeManifest* m) {
    copyStr(m.name.ptr, "".ptr, m.name.length);
    copyStr(m.ver.ptr, "1.0.0".ptr, m.ver.length);
    copyStr(m.arch.ptr, "x86_64".ptr, m.arch.length);
    copyStr(m.section.ptr, "misc".ptr, m.section.length);
    copyStr(m.license.ptr, "unknown".ptr, m.license.length);
    copyStr(m.summary.ptr, "".ptr, m.summary.length);
    copyStr(m.description.ptr, "".ptr, m.description.length);
    copyStr(m.source.ptr, "".ptr, m.source.length);
    copyStr(m.build.ptr, "custom".ptr, m.build.length);
    m.depCount = 0;
}

private void parseRecipeDepends(const char* value, RecipeManifest* m) {
    char[512] tmp = void;
    ulong i = 0;
    m.depCount = 0;
    copyStr(tmp.ptr, value, tmp.length);
    while (tmp[i] != 0 && m.depCount < m.depends.length) {
        ulong start;
        ulong end;
        while (tmp[i] != 0 && (tmp[i] == ',' || isSpace(tmp[i]) != 0)) i++;
        if (tmp[i] == 0) break;
        start = i;
        while (tmp[i] != 0 && tmp[i] != ',') i++;
        end = i;
        while (end > start && isSpace(tmp[end - 1]) != 0) end--;
        while (start < end && isSpace(tmp[start]) != 0) start++;
        if (end > start) {
            char[64] dep = void;
            ulong d = 0;
            while (start < end && d + 1 < dep.length) dep[d++] = tmp[start++];
            dep[d] = 0;
            if (isSafePkgName(dep.ptr) != 0) {
                copyStr(m.depends[m.depCount].ptr, dep.ptr, m.depends[m.depCount].length);
                m.depCount++;
            }
        }
        if (tmp[i] == ',') i++;
    }
}

private int parseRecipeFile(const char* path, RecipeManifest* manifest) {
    char[16384] buf = void;
    char* p;
    int n = readFile(path, buf.ptr, buf.length);
    if (n < 0) {
        printf("opkg recipe: cannot read recipe: %s\n", path);
        return -1;
    }
    recipeDefaults(manifest);
    p = buf.ptr;
    while (*p != 0) {
        char* line = p;
        char* key;
        char* val;
        while (*p != 0 && *p != '\n') p++;
        if (*p == '\n') {
            *p = 0;
            p++;
        }
        line = trimInPlace(line);
        if (*line == 0 || *line == '#') continue;
        key = line;
        while (*line != 0 && *line != '=') line++;
        if (*line != '=') continue;
        *line = 0;
        line++;
        key = trimInPlace(key);
        val = trimInPlace(line);
        if (streq(key, "name".ptr)) copyStr(manifest.name.ptr, val, manifest.name.length);
        else if (streq(key, "version".ptr)) copyStr(manifest.ver.ptr, val, manifest.ver.length);
        else if (streq(key, "arch".ptr)) copyStr(manifest.arch.ptr, val, manifest.arch.length);
        else if (streq(key, "section".ptr)) copyStr(manifest.section.ptr, val, manifest.section.length);
        else if (streq(key, "license".ptr)) copyStr(manifest.license.ptr, val, manifest.license.length);
        else if (streq(key, "summary".ptr)) copyStr(manifest.summary.ptr, val, manifest.summary.length);
        else if (streq(key, "description".ptr)) copyStr(manifest.description.ptr, val, manifest.description.length);
        else if (streq(key, "source".ptr)) copyStr(manifest.source.ptr, val, manifest.source.length);
        else if (streq(key, "build".ptr)) copyStr(manifest.build.ptr, val, manifest.build.length);
        else if (streq(key, "depends".ptr)) parseRecipeDepends(val, manifest);
    }

    if (isSafePkgName(manifest.name.ptr) == 0) {
        printf("opkg recipe: invalid or missing name\n");
        return -1;
    }
    if (manifest.ver[0] == 0) {
        printf("opkg recipe: missing version\n");
        return -1;
    }
    if (manifest.summary[0] == 0) {
        printf("opkg recipe: missing summary\n");
        return -1;
    }
    if (manifest.build[0] == 0) {
        printf("opkg recipe: missing build system\n");
        return -1;
    }
    if (streq(manifest.arch.ptr, "x86_64".ptr) == 0 && streq(manifest.arch.ptr, "noarch".ptr) == 0) {
        printf("opkg recipe: unsupported arch: %s\n", manifest.arch.ptr);
        return -1;
    }
    return 0;
}

private int cmdRecipeValidate(const char* path) {
    RecipeManifest m = void;
    if (parseRecipeFile(path, &m) < 0) return 1;
    printf("recipe valid: %s %s (%s)\n", m.name.ptr, m.ver.ptr, m.arch.ptr);
    return 0;
}

private int cmdRecipeShow(const char* path) {
    RecipeManifest m = void;
    if (parseRecipeFile(path, &m) < 0) return 1;
    printf("Name: %s\n", m.name.ptr);
    printf("Version: %s\n", m.ver.ptr);
    printf("Arch: %s\n", m.arch.ptr);
    printf("Section: %s\n", m.section.ptr);
    printf("License: %s\n", m.license.ptr);
    printf("Build: %s\n", m.build.ptr);
    printf("Source: %s\n", m.source.ptr);
    printf("Summary: %s\n", m.summary.ptr);
    printf("Description: %s\n", m.description.ptr);
    printf("Depends (%d):", m.depCount);
    for (int i = 0; i < m.depCount; i++) {
        printf(" %s", m.depends[i].ptr);
    }
    printf("\n");
    return 0;
}

private int cmdRecipeScaffold(const char* path, const char* name) {
    char[4096] text = void;
    char[64] safe = void;
    if (isSafePkgName(name) == 0) {
        printf("opkg recipe scaffold: invalid package name\n");
        return 1;
    }
    copyStr(safe.ptr, name, safe.length);
    copyStr(text.ptr, "# Obelisk opkg recipe (DSL v0)\n".ptr, text.length);
    appendStr(text.ptr, "name=".ptr, text.length);
    appendStr(text.ptr, safe.ptr, text.length);
    appendStr(text.ptr, "\nversion=1.0.0\narch=x86_64\nsection=custom\nlicense=MIT\n".ptr, text.length);
    appendStr(text.ptr, "summary=".ptr, text.length);
    appendStr(text.ptr, safe.ptr, text.length);
    appendStr(text.ptr, " package\n".ptr, text.length);
    appendStr(text.ptr, "description=Custom package built for Obelisk\n".ptr, text.length);
    appendStr(text.ptr, "source=https://example.invalid/".ptr, text.length);
    appendStr(text.ptr, safe.ptr, text.length);
    appendStr(text.ptr, ".tar.gz\n".ptr, text.length);
    appendStr(text.ptr, "build=custom\n".ptr, text.length);
    appendStr(text.ptr, "depends=\n".ptr, text.length);
    if (writeFile(path, text.ptr) < 0) {
        printf("opkg recipe scaffold: cannot write %s\n", path);
        return 1;
    }
    printf("wrote recipe scaffold: %s\n", path);
    return 0;
}

private int cmdRecipe(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: opkg recipe <validate|show|scaffold> ...\n");
        printf("  opkg recipe validate <file.opkrecipe>\n");
        printf("  opkg recipe show <file.opkrecipe>\n");
        printf("  opkg recipe scaffold <path.opkrecipe> <name>\n");
        return 1;
    }
    if (streq(argv[2], "validate".ptr)) {
        if (argc < 4) {
            printf("usage: opkg recipe validate <file.opkrecipe>\n");
            return 1;
        }
        return cmdRecipeValidate(argv[3]);
    }
    if (streq(argv[2], "show".ptr)) {
        if (argc < 4) {
            printf("usage: opkg recipe show <file.opkrecipe>\n");
            return 1;
        }
        return cmdRecipeShow(argv[3]);
    }
    if (streq(argv[2], "scaffold".ptr)) {
        if (argc < 5) {
            printf("usage: opkg recipe scaffold <path.opkrecipe> <name>\n");
            return 1;
        }
        return cmdRecipeScaffold(argv[3], argv[4]);
    }
    printf("opkg recipe: unknown action: %s\n", argv[2]);
    return 1;
}

private int installFirstAvailable(const char** names, int n) {
    RepoIndexEntry ent = void;
    RepoConfigEntry repo = void;
    for (int i = 0; i < n; i++) {
        if (findBestRepoPackage(names[i], &ent, &repo) == 0) {
            return installFromRepoName(names[i]) == 0 ? 0 : -1;
        }
    }
    return -1;
}

private void installOptionalFirstAvailable(const char** names, int n) {
    if (installFirstAvailable(names, n) < 0) {
        if (n > 0 && names !is null) {
            printf("opkg profile: optional package group not found (first candidate: %s)\n", names[0]);
        }
    }
}

private int cmdInstallProfile(const char* profile) {
    if (streq(profile, "xorg".ptr)) {
        const(char)*[3] xorgCandidates = [ "xorg".ptr, "xorg-base".ptr, "xorg-server".ptr ];
        const(char)*[3] xinitCandidates = [ "xinit".ptr, "xinit-real".ptr, "x11-xinit".ptr ];
        const(char)*[3] desktopCandidates = [ "desktop-base".ptr, "x11-runtime".ptr, "desktop-runtime".ptr ];
        if (installFirstAvailable(xorgCandidates.ptr, xorgCandidates.length) < 0) {
            printf("opkg profile: no xorg package found in cached repositories\n");
            return 1;
        }
        if (installFirstAvailable(xinitCandidates.ptr, xinitCandidates.length) < 0) {
            printf("opkg profile: xorg profile requires xinit package in repositories\n");
            return 1;
        }
        installOptionalFirstAvailable(desktopCandidates.ptr, desktopCandidates.length);
        return 0;
    }
    if (streq(profile, "xfce".ptr)) {
        const(char)*[3] xorgCandidates = [ "xorg".ptr, "xorg-base".ptr, "xorg-server".ptr ];
        const(char)*[3] xinitCandidates = [ "xinit".ptr, "xinit-real".ptr, "x11-xinit".ptr ];
        const(char)*[3] desktopCandidates = [ "desktop-base".ptr, "x11-runtime".ptr, "desktop-runtime".ptr ];
        const(char)*[3] gtkCandidates = [ "gtk3-runtime".ptr, "gtk-runtime".ptr, "gtk-stack".ptr ];
        const(char)*[5] xfceProfileCandidates = [ "xfce-profile".ptr, "xfce".ptr, "xfce4-profile".ptr, "desktop-session-profile".ptr, "xfce-desktop".ptr ];
        const(char)*[3] xfceRuntimeCandidates = [ "xfce-runtime".ptr, "xfce4".ptr, "xfce-session".ptr ];
        if (installFirstAvailable(xorgCandidates.ptr, xorgCandidates.length) < 0) {
            printf("opkg profile: xfce requires xorg package in repositories\n");
            return 1;
        }
        if (installFirstAvailable(xinitCandidates.ptr, xinitCandidates.length) < 0) {
            printf("opkg profile: xfce requires xinit package in repositories\n");
            return 1;
        }
        installOptionalFirstAvailable(desktopCandidates.ptr, desktopCandidates.length);
        installOptionalFirstAvailable(gtkCandidates.ptr, gtkCandidates.length);
        if (installFirstAvailable(xfceRuntimeCandidates.ptr, xfceRuntimeCandidates.length) < 0) {
            printf("opkg profile: xfce requires xfce-runtime package in repositories\n");
            return 1;
        }
        if (installFirstAvailable(xfceProfileCandidates.ptr, xfceProfileCandidates.length) < 0) {
            printf("opkg profile: no xfce profile package found in cached repositories\n");
            return 1;
        }
        return 0;
    }
    if (streq(profile, "xdm".ptr)) {
        const(char)*[3] xorgCandidates = [ "xorg".ptr, "xorg-base".ptr, "xorg-server".ptr ];
        const(char)*[3] xinitCandidates = [ "xinit".ptr, "xinit-real".ptr, "x11-xinit".ptr ];
        const(char)*[3] desktopCandidates = [ "desktop-base".ptr, "x11-runtime".ptr, "desktop-runtime".ptr ];
        const(char)*[3] gtkCandidates = [ "gtk3-runtime".ptr, "gtk-runtime".ptr, "gtk-stack".ptr ];
        const(char)*[5] xfceProfileCandidates = [ "xfce-profile".ptr, "xfce".ptr, "xfce4-profile".ptr, "desktop-session-profile".ptr, "xfce-desktop".ptr ];
        const(char)*[3] xfceRuntimeCandidates = [ "xfce-runtime".ptr, "xfce4".ptr, "xfce-session".ptr ];
        const(char)*[4] xdmCandidates = [ "xdm".ptr, "x11-xdm".ptr, "xorg-xdm".ptr, "display-manager".ptr ];
        const(char)*[4] xdmProfileCandidates = [ "xdm-profile".ptr, "desktop-profile".ptr, "display-manager-profile".ptr, "xdm-meta".ptr ];
        if (installFirstAvailable(xorgCandidates.ptr, xorgCandidates.length) < 0) {
            printf("opkg profile: xdm requires xorg package in repositories\n");
            return 1;
        }
        if (installFirstAvailable(xinitCandidates.ptr, xinitCandidates.length) < 0) {
            printf("opkg profile: xdm requires xinit package in repositories\n");
            return 1;
        }
        installOptionalFirstAvailable(desktopCandidates.ptr, desktopCandidates.length);
        installOptionalFirstAvailable(gtkCandidates.ptr, gtkCandidates.length);
        if (installFirstAvailable(xfceRuntimeCandidates.ptr, xfceRuntimeCandidates.length) < 0) {
            printf("opkg profile: xdm requires xfce-runtime package in repositories\n");
            return 1;
        }
        installOptionalFirstAvailable(xfceProfileCandidates.ptr, xfceProfileCandidates.length);
        if (installFirstAvailable(xdmCandidates.ptr, xdmCandidates.length) < 0) {
            printf("opkg profile: no xdm runtime package found in repositories\n");
            return 1;
        }
        installOptionalFirstAvailable(xdmProfileCandidates.ptr, xdmProfileCandidates.length);
        return 0;
    }
    printf("opkg profile: unknown profile '%s' (supported: xorg, xfce, xdm)\n", profile);
    return 1;
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
    printf("  update                 Refresh repo cache (file://, local path, or http/https)\n");
    printf("  search <term>          Search cached repository metadata\n");
    printf("  repo                   List configured repositories\n");
    printf("  doctor                 Show runtime capability diagnostics\n");
    printf("  install-profile <name> Install package profile (xorg, xfce, xdm)\n");
    printf("  recipe ...             Recipe DSL tools (validate/show/scaffold)\n");
    printf("\nrepos.conf format:\n");
    printf("  <name> <base-url-or-path> [sha256:<index-json-hash>]\n");
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
    if (streq(cmd, "install-profile".ptr)) {
        if (argc < 3) {
            printf("usage: opkg install-profile <xorg|xfce|xdm>\n");
            return 1;
        }
        return cmdInstallProfile(argv[2]);
    }
    if (streq(cmd, "recipe".ptr)) {
        return cmdRecipe(argc, argv);
    }
    printf("opkg: unknown command: %s\n", cmd);
    return 1;
}
