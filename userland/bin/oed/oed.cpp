/*
 * oed - Obelisk line editor (ed-like)
 * Minimal but correct C++ ed clone for Obelisk OS.
 * Uses raw syscalls; no libc dependency.
 */

#include <stdint.h>
#include <stddef.h>

/* ---- syscall numbers (x86-64) ---- */
static constexpr long SYS_READ  = 0;
static constexpr long SYS_WRITE = 1;
static constexpr long SYS_OPEN  = 2;
static constexpr long SYS_CLOSE = 3;
static constexpr long SYS_EXIT  = 60;

static constexpr long O_RDONLY = 0x0000;
static constexpr long O_WRONLY = 0x0001;
static constexpr long O_CREAT  = 0x0040;
static constexpr long O_TRUNC  = 0x0200;

/* ---- limits ---- */
static constexpr int MAX_LINES    = 4096;
static constexpr int MAX_LINE_LEN = 512;
static constexpr int CMD_BUF      = 512;
static constexpr int IO_BUF       = 65536;

/* ---- editor state ---- */
static char g_lines[MAX_LINES][MAX_LINE_LEN];
static int  g_nlines   = 0;     /* total lines in buffer          */
static int  g_dot      = 0;     /* current line (1-based, 0=none) */
static bool g_dirty    = false;  /* unsaved modifications          */
static bool g_warned   = false;  /* printed "?" for unsaved quit   */
static char g_path[256];
static bool g_has_path = false;
static bool g_verbose  = false;  /* 'H' toggles verbose errors     */

/* ---- syscall wrappers ---- */
static long call1(long num, long a1)
{
    long ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret) : "a"(num), "D"(a1)
        : "cc", "memory");
    return ret;
}

static long call3(long num, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "cc", "memory");
    return ret;
}

/* ---- string helpers ---- */
static size_t slen(const char *s)
{
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void scopy(char *d, const char *s, size_t cap)
{
    if (!d || cap == 0) return;
    size_t i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int scmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_ws(char c)    { return c == ' ' || c == '\t'; }

/* ---- I/O helpers ---- */
static void wr(int fd, const char *s, size_t n)
{
    while (n > 0) {
        long r = call3(SYS_WRITE, fd, (long)s, (long)n);
        if (r <= 0) break;
        s += r; n -= (size_t)r;
    }
}

static void puts1(const char *s) { wr(1, s, slen(s)); }
static void puts2(const char *s) { wr(2, s, slen(s)); }
static void putc1(char c)        { wr(1, &c, 1); }

static void putu(unsigned long v)
{
    char buf[24]; int n = 0;
    if (v == 0) { buf[n++] = '0'; }
    else {
        char rev[24]; int r = 0;
        while (v) { rev[r++] = '0' + (char)(v % 10); v /= 10; }
        while (r) buf[n++] = rev[--r];
    }
    wr(1, buf, (size_t)n);
}

static void err(const char *msg)
{
    puts1("?\n");
    if (g_verbose && msg) { puts2(msg); puts2("\n"); }
}

/* ---- line input ---- */
static bool read_line(char *out, size_t cap, bool local_echo)
{
    size_t len = 0;
    while (len + 1 < cap) {
        char c = 0;
        long r = call3(SYS_READ, 0, (long)&c, 1);
        if (r <= 0) { if (len == 0) return false; break; }
        if (c == '\r') continue;
        if (c == '\n') {
            if (local_echo) putc1('\n');
            break;
        }
        if (c == 0x7f || c == '\b') {
            if (len > 0) {
                len--;
                if (local_echo) puts1("\b \b");
            }
            continue;
        }
        out[len++] = c;
        if (local_echo) putc1(c);
    }
    out[len] = '\0';
    return true;
}

/* ---- address parser ---- */
static void skip_ws(const char **pp)
{
    while (is_ws(**pp)) (*pp)++;
}

/* Parse a single address token: '.', '$', N, or nothing.
   Returns true if an address was consumed and sets *val. */
static bool addr_token(const char **pp, int *val)
{
    const char *p = *pp;
    if (*p == '.') { *val = g_dot; p++; *pp = p; return true; }
    if (*p == '$') { *val = g_nlines; p++; *pp = p; return true; }
    if (is_digit(*p)) {
        int v = 0;
        while (is_digit(*p)) { v = v * 10 + (*p - '0'); p++; }
        *val = v; *pp = p; return true;
    }
    return false;
}

/* Parse optional address prefix:  N | N,M | . | $ | %
   Sets a1..a2 and returns true.  If no address, a1=a2=g_dot. */
struct Addr { int a1, a2; bool explicit_range; };

static bool parse_addr(const char **pp, Addr *out)
{
    const char *p = *pp;
    skip_ws(&p);

    /* % = 1,$ shorthand */
    if (*p == '%') {
        p++;
        out->a1 = 1;
        out->a2 = g_nlines;
        out->explicit_range = true;
        *pp = p;
        return true;
    }

    int v1 = g_dot;
    bool got1 = addr_token(&p, &v1);
    skip_ws(&p);

    if (*p == ',') {
        p++; skip_ws(&p);
        int v2 = 0;
        if (!addr_token(&p, &v2)) { return false; }
        out->a1 = v1; out->a2 = v2;
        out->explicit_range = true;
        *pp = p;
        return true;
    }

    if (got1) {
        out->a1 = out->a2 = v1;
        out->explicit_range = true;
    } else {
        out->a1 = out->a2 = g_dot;
        out->explicit_range = false;
    }
    *pp = p;
    return true;
}

/* Validate address range against current buffer */
static bool valid_addr(int a1, int a2)
{
    return a1 >= 1 && a2 >= 1 && a1 <= g_nlines && a2 <= g_nlines && a1 <= a2;
}

/* ---- buffer operations ---- */
static bool insert_at(int idx0, const char *text)
{
    if (idx0 < 0 || idx0 > g_nlines || g_nlines >= MAX_LINES) return false;
    for (int i = g_nlines; i > idx0; i--)
        scopy(g_lines[i], g_lines[i - 1], MAX_LINE_LEN);
    scopy(g_lines[idx0], text, MAX_LINE_LEN);
    g_nlines++;
    g_dot = idx0 + 1;
    g_dirty = true;
    return true;
}

static bool delete_range(int a1, int a2)
{
    if (!valid_addr(a1, a2)) return false;
    int count = a2 - a1 + 1;
    int idx   = a1 - 1;
    for (int i = idx; i + count < g_nlines; i++)
        scopy(g_lines[i], g_lines[i + count], MAX_LINE_LEN);
    g_nlines -= count;
    if (g_nlines == 0) g_dot = 0;
    else if (idx < g_nlines) g_dot = idx + 1;
    else g_dot = g_nlines;
    g_dirty = true;
    return true;
}

/* ---- input mode (append lines until lone '.') ---- */
static void input_mode(int after_idx0, const char *label)
{
    int idx = after_idx0;
    puts1("-- ");
    puts1(label ? label : "INPUT");
    puts1(" -- (end with single '.')\n");
    for (;;) {
        char buf[MAX_LINE_LEN];
        puts1("> ");
        if (!read_line(buf, sizeof(buf), true)) return;
        if (scmp(buf, ".") == 0) break;
        if (!insert_at(idx, buf)) { err("buffer full"); return; }
        idx++;
    }
    puts1("-- end --\n");
}

/* ---- file I/O ---- */
static long count_bytes(void)
{
    long total = 0;
    for (int i = 0; i < g_nlines; i++)
        total += (long)slen(g_lines[i]) + 1;
    return total;
}

static bool load_file(const char *path)
{
    char buf[IO_BUF];
    size_t used = 0;
    long fd = call3(SYS_OPEN, (long)path, O_RDONLY, 0);
    if (fd < 0) return false;

    while (used < sizeof(buf) - 1) {
        long n = call3(SYS_READ, fd, (long)(buf + used),
                       (long)(sizeof(buf) - 1 - used));
        if (n < 0) { call1(SYS_CLOSE, fd); return false; }
        if (n == 0) break;
        used += (size_t)n;
    }
    call1(SYS_CLOSE, fd);
    buf[used] = '\0';

    g_nlines = 0; g_dot = 0;
    size_t start = 0;
    for (size_t i = 0; i <= used && g_nlines < MAX_LINES; i++) {
        if (i == used || buf[i] == '\n') {
            /* skip empty trailing line caused by final newline */
            if (i == used && i == start && used > 0 && buf[used - 1] == '\n')
                break;
            size_t len = i - start;
            if (len >= (size_t)MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
            for (size_t k = 0; k < len; k++)
                g_lines[g_nlines][k] = buf[start + k];
            g_lines[g_nlines][len] = '\0';
            g_nlines++;
            start = i + 1;
        }
    }
    if (g_nlines > 0) g_dot = g_nlines;
    g_dirty = false;
    return true;
}

static bool write_file(const char *path)
{
    long fd = call3(SYS_OPEN, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    for (int i = 0; i < g_nlines; i++) {
        size_t n = slen(g_lines[i]);
        if (n > 0 && call3(SYS_WRITE, fd, (long)g_lines[i], (long)n) < 0)
            { call1(SYS_CLOSE, fd); return false; }
        if (call3(SYS_WRITE, fd, (long)"\n", 1) < 0)
            { call1(SYS_CLOSE, fd); return false; }
    }
    call1(SYS_CLOSE, fd);
    g_dirty = false;
    return true;
}

/* ---- print helpers ---- */
static void print_line(int one, bool numbered)
{
    if (one < 1 || one > g_nlines) return;
    if (numbered) { putu((unsigned long)one); putc1('\t'); }
    puts1(g_lines[one - 1]);
    putc1('\n');
}

/* ---- help ---- */
static void print_help(void)
{
    puts1(
        "oed - Obelisk line editor\n"
        "Commands (A = address/range, default = current line):\n"
        "  [A]p      print lines\n"
        "  [A]n      print with line numbers\n"
        "  [A]a      append text after A (end input with '.')\n"
        "  [A]i      insert text before A\n"
        "  [A]c      change (replace) lines A\n"
        "  [A]d      delete lines A\n"
        "  [A]m[N]   move lines A to after line N\n"
        "  [A]j      join lines A into one line\n"
        "  =         print current line number\n"
        "  f [FILE]  show/set filename\n"
        "  e [FILE]  edit file (replaces buffer)\n"
        "  r [FILE]  read file (appends to buffer)\n"
        "  w [FILE]  write buffer to file\n"
        "  wq        write and quit\n"
        "  q         quit (warns if unsaved)\n"
        "  Q         force quit\n"
        "  H         toggle verbose errors\n"
        "  h         show this help\n"
        "Addresses: N  .  $  %  N,M\n"
    );
}

/* ---- command dispatch ---- */
static void do_command(char *raw)
{
    /* trim trailing whitespace */
    { size_t n = slen(raw);
      while (n > 0 && (raw[n-1]==' '||raw[n-1]=='\t'||raw[n-1]=='\r'||raw[n-1]=='\n'))
          raw[--n] = '\0';
    }
    const char *p = raw;
    while (is_ws(*p)) p++;

    /* empty line: advance dot, print */
    if (*p == '\0') {
        if (g_dot >= 1 && g_dot < g_nlines) {
            g_dot++;
            print_line(g_dot, false);
        } else if (g_dot >= 1 && g_dot <= g_nlines) {
            print_line(g_dot, false);
        } else {
            err("empty buffer");
        }
        return;
    }

    /* parse optional address prefix */
    Addr addr;
    if (!parse_addr(&p, &addr)) { err("bad address"); return; }
    skip_ws(&p);

    char cmd = *p ? *p++ : '\0';

    /* bare address: set dot and print */
    if (cmd == '\0') {
        if (!addr.explicit_range) { err("unknown command"); return; }
        if (!valid_addr(addr.a2, addr.a2) && g_nlines == 0)
            { err("empty buffer"); return; }
        if (!valid_addr(addr.a2, addr.a2))
            { err("invalid address"); return; }
        g_dot = addr.a2;
        print_line(g_dot, false);
        g_warned = false;
        return;
    }

    /* ---- commands ---- */

    if (cmd == 'h') {
        print_help();
        g_warned = false;
        return;
    }
    if (cmd == 'H') {
        g_verbose = !g_verbose;
        g_warned = false;
        return;
    }
    if (cmd == '=') {
        putu((unsigned long)((addr.explicit_range) ? addr.a2
              : ((g_nlines > 0) ? g_nlines : 0)));
        putc1('\n');
        g_warned = false;
        return;
    }

    /* p / n — print */
    if (cmd == 'p' || cmd == 'n') {
        int a1 = addr.a1, a2 = addr.a2;
        if (!addr.explicit_range) { a1 = a2 = g_dot; }
        if (!valid_addr(a1, a2)) { err("invalid address"); return; }
        for (int i = a1; i <= a2; i++) print_line(i, cmd == 'n');
        g_dot = a2;
        g_warned = false;
        return;
    }

    /* d — delete */
    if (cmd == 'd') {
        int a1 = addr.a1, a2 = addr.a2;
        if (!addr.explicit_range) { a1 = a2 = g_dot; }
        if (!valid_addr(a1, a2)) { err("invalid address"); return; }
        delete_range(a1, a2);
        if (g_dot >= 1 && g_dot <= g_nlines) print_line(g_dot, false);
        g_warned = false;
        return;
    }

    /* a — append after */
    if (cmd == 'a') {
        int target = addr.explicit_range ? addr.a2 : g_dot;
        if (target < 0) target = 0;
        if (target > g_nlines) { err("invalid address"); return; }
        input_mode(target, "APPEND");  /* insert after line target (0-based idx = target) */
        g_warned = false;
        return;
    }

    /* i — insert before */
    if (cmd == 'i') {
        int target = addr.explicit_range ? addr.a1 : g_dot;
        if (target < 1) target = 1;
        if (target > g_nlines + 1) { err("invalid address"); return; }
        input_mode(target - 1, "INSERT");  /* 0-based idx before target */
        g_warned = false;
        return;
    }

    /* c — change (delete then insert) */
    if (cmd == 'c') {
        int a1 = addr.a1, a2 = addr.a2;
        if (!addr.explicit_range) { a1 = a2 = g_dot; }
        if (!valid_addr(a1, a2)) { err("invalid address"); return; }
        delete_range(a1, a2);
        /* insert where the deleted lines were */
        int ins_idx = a1 - 1;
        if (ins_idx < 0) ins_idx = 0;
        input_mode(ins_idx, "CHANGE");
        g_warned = false;
        return;
    }

    /* j — join lines */
    if (cmd == 'j') {
        int a1 = addr.a1, a2 = addr.a2;
        if (!addr.explicit_range) {
            a1 = g_dot; a2 = g_dot + 1;
        }
        if (!valid_addr(a1, a2) || a1 == a2)
            { err("invalid address"); return; }
        /* concatenate into a1 */
        char joined[MAX_LINE_LEN];
        scopy(joined, g_lines[a1 - 1], MAX_LINE_LEN);
        for (int i = a1 + 1; i <= a2; i++) {
            size_t cur = slen(joined);
            if (cur + 1 < (size_t)MAX_LINE_LEN)
                scopy(joined + cur, g_lines[i - 1],
                      (size_t)MAX_LINE_LEN - cur);
        }
        scopy(g_lines[a1 - 1], joined, MAX_LINE_LEN);
        /* delete lines a1+1..a2 */
        if (a1 + 1 <= a2) delete_range(a1 + 1, a2);
        g_dot = a1;
        g_dirty = true;
        g_warned = false;
        return;
    }

    /* m — move lines */
    if (cmd == 'm') {
        skip_ws(&p);
        int dest = 0;
        if (!addr_token(&p, &dest)) { err("bad destination"); return; }
        int a1 = addr.a1, a2 = addr.a2;
        if (!addr.explicit_range) { a1 = a2 = g_dot; }
        if (!valid_addr(a1, a2))
            { err("invalid address"); return; }
        if (dest < 0 || dest > g_nlines)
            { err("bad destination"); return; }
        if (dest >= a1 && dest <= a2)
            { err("move into self"); return; }

        /* copy lines out */
        int count = a2 - a1 + 1;
        char tmp[64][MAX_LINE_LEN];  /* limited by stack, keep small */
        if (count > 64) { err("range too large for move"); return; }
        for (int i = 0; i < count; i++)
            scopy(tmp[i], g_lines[a1 - 1 + i], MAX_LINE_LEN);
        delete_range(a1, a2);
        /* adjust dest after deletion */
        if (dest > a2) dest -= count;
        for (int i = 0; i < count; i++) {
            if (!insert_at(dest + i, tmp[i]))
                { err("buffer full"); return; }
        }
        g_dot = dest + count;
        g_dirty = true;
        g_warned = false;
        return;
    }

    /* f — filename */
    if (cmd == 'f') {
        skip_ws(&p);
        if (*p) {
            scopy(g_path, p, sizeof(g_path));
            g_has_path = true;
        }
        if (g_has_path) { puts1(g_path); putc1('\n'); }
        else err("no filename");
        g_warned = false;
        return;
    }

    /* e — edit (replace buffer) */
    if (cmd == 'e') {
        if (g_dirty && !g_warned) { g_warned = true; err("unsaved changes"); return; }
        skip_ws(&p);
        if (*p) { scopy(g_path, p, sizeof(g_path)); g_has_path = true; }
        if (!g_has_path) { err("no filename"); return; }
        if (!load_file(g_path)) { err("cannot open file"); return; }
        putu((unsigned long)count_bytes());
        putc1('\n');
        g_warned = false;
        return;
    }

    /* r — read (append) */
    if (cmd == 'r') {
        skip_ws(&p);
        const char *rpath = *p ? p : (g_has_path ? g_path : nullptr);
        if (!rpath) { err("no filename"); return; }

        char buf[IO_BUF];
        size_t used = 0;
        long fd = call3(SYS_OPEN, (long)rpath, O_RDONLY, 0);
        if (fd < 0) { err("cannot open file"); return; }
        while (used < sizeof(buf) - 1) {
            long n = call3(SYS_READ, fd, (long)(buf + used),
                           (long)(sizeof(buf) - 1 - used));
            if (n <= 0) break;
            used += (size_t)n;
        }
        call1(SYS_CLOSE, fd);
        buf[used] = '\0';

        int after = addr.explicit_range ? addr.a2 : g_nlines;
        size_t start = 0;
        long bytes = 0;
        for (size_t i = 0; i <= used && g_nlines < MAX_LINES; i++) {
            if (i == used || buf[i] == '\n') {
                if (i == used && i == start && used > 0 && buf[used - 1] == '\n')
                    break;
                size_t len = i - start;
                if (len >= (size_t)MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
                char tmp[MAX_LINE_LEN];
                for (size_t k = 0; k < len; k++) tmp[k] = buf[start + k];
                tmp[len] = '\0';
                insert_at(after, tmp);
                after++;
                bytes += (long)len + 1;
                start = i + 1;
            }
        }
        putu((unsigned long)bytes);
        putc1('\n');
        g_warned = false;
        return;
    }

    /* w / wq — write */
    if (cmd == 'w') {
        bool do_quit = false;
        const char *wpath = nullptr;

        skip_ws(&p);
        if (*p == 'q' && (p[1] == '\0' || is_ws(p[1]))) {
            do_quit = true;
            p++;
            skip_ws(&p);
        }
        if (*p) {
            wpath = p;
            scopy(g_path, wpath, sizeof(g_path));
            g_has_path = true;
        } else {
            wpath = g_has_path ? g_path : nullptr;
        }

        if (!wpath) { err("no filename (use: f <file> or w <file>)"); return; }
        if (!write_file(wpath)) { err("write failed"); return; }
        putu((unsigned long)count_bytes());
        putc1('\n');

        if (do_quit) { call1(SYS_EXIT, 0); __builtin_unreachable(); }
        g_warned = false;
        return;
    }

    /* q — quit */
    if (cmd == 'q') {
        if (g_dirty && !g_warned) { g_warned = true; err("unsaved changes"); return; }
        call1(SYS_EXIT, 0);
        __builtin_unreachable();
    }

    /* Q — force quit */
    if (cmd == 'Q') {
        call1(SYS_EXIT, 0);
        __builtin_unreachable();
    }

    g_warned = false;
    err("unknown command");
}

/* ---- entry point ---- */
extern "C" void _start(void)
{
    uint64_t *sp;
    __asm__ volatile("movq %%rsp, %0" : "=r"(sp));
    int argc = (int)sp[0];
    char **argv = (char **)&sp[1];

    if (argc >= 2 && argv[1] && argv[1][0]) {
        scopy(g_path, argv[1], sizeof(g_path));
        g_has_path = true;
        if (load_file(g_path)) {
            putu((unsigned long)count_bytes());
            putc1('\n');
        } else {
            puts1(g_path);
            puts1(": new file\n");
        }
    }

    for (;;) {
        char cmd[CMD_BUF];
        puts1("oed> ");
        if (!read_line(cmd, sizeof(cmd), true)) {
            /* EOF */
            if (g_dirty) { puts1("\n"); err("unsaved changes"); continue; }
            call1(SYS_EXIT, 0);
            __builtin_unreachable();
        }
        do_command(cmd);
    }
}
