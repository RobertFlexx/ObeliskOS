//! Obelisk pathname / sysctl input policy (Zig).
//!
//! Bounded, deterministic scan — no heap. Used after `copy_user_cstring` fills a kernel buffer.
//!
//! Policy:
//! - Non-empty string (first byte not NUL).
//! - Terminating NUL must appear within `cap` bytes (scan bounded by `@min(cap, MAX_SCAN)`).
//! - Reject C0 control bytes (0x01..0x1f) and DEL (0x7f) before the terminator.
//! - Allow space (0x20) and bytes >= 0x80 (e.g. UTF-8 continuation bytes in paths).
//!
//! Returns 0 if accepted, non-zero if rejected (C side maps to -EINVAL).

const MAX_SCAN: u64 = 16384;

pub export fn zig_kernel_cstring_no_control(s: ?[*]const u8, cap: u64) c_int {
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
        if (c < 0x20) return 1;
        if (c == 0x7f) return 1;
    }
    return 1;
}
