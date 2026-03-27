//! Bounded validation for kernel-held exec-related strings (argv/env copies).
//! Complements path_validate.zig: allows '=' for KEY=value env lines.

const MAX_SCAN: u64 = 4096;
/// Upper bound for scanning (matches MAX_RW_COUNT order of magnitude).
const MAX_NUL_SCAN: u64 = 0x7ffff000;

/// 0 if s is non-empty, NUL-terminated within cap, no C0 controls except TAB (0x09), no DEL.
/// Allows '=' (env vars). Rejects other C0 (0x01..0x08, 0x0a..0x1f).
pub export fn zig_kernel_exec_line_ok(s: ?[*]const u8, cap: u64) c_int {
    if (s == null) return 1;
    if (cap == 0) return 1;
    if (cap > MAX_SCAN) return 1;
    const p = s.?;
    const limit = cap;
    var i: usize = 0;
    while (i < limit) : (i += 1) {
        const c = p[i];
        if (c == 0) {
            if (i == 0) return 1;
            return 0;
        }
        if (c < 0x20 and c != 0x09) return 1;
        if (c == 0x7f) return 1;
    }
    return 1;
}

/// Index of first NUL in buf[0..len), or -1 if none (len bounded for safety).
pub export fn zig_cstring_first_nul_index(buf: ?[*]const u8, len: u64) i64 {
    if (buf == null) return -1;
    if (len == 0) return -1;
    if (len > MAX_NUL_SCAN) return -1;
    const p = buf.?;
    const lim = len;
    var i: u64 = 0;
    while (i < lim) : (i += 1) {
        if (p[@as(usize, @intCast(i))] == 0) {
            return @as(i64, @intCast(i));
        }
    }
    return -1;
}
