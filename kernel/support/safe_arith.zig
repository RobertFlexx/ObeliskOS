//! Bounded 64-bit arithmetic for kernel C callers (overflow-detecting).
//!
//! Used where manual overflow checks are easy to get wrong or miss.

pub export fn zig_u64_mul_ok(a: u64, b: u64, out: *u64) c_int {
    if (@intFromPtr(out) == 0) return 1;
    const r = @mulWithOverflow(a, b);
    out.* = r[0];
    return if (r[1] != 0) 1 else 0;
}

pub export fn zig_u64_add_ok(a: u64, b: u64, out: *u64) c_int {
    if (@intFromPtr(out) == 0) return 1;
    const r = @addWithOverflow(a, b);
    out.* = r[0];
    return if (r[1] != 0) 1 else 0;
}

pub export fn zig_u64_sub_ok(a: u64, b: u64, out: *u64) c_int {
    if (@intFromPtr(out) == 0) return 1;
    const r = @subWithOverflow(a, b);
    out.* = r[0];
    return if (r[1] != 0) 1 else 0;
}

/// Linux-compatible upper bound for a single read/write/copy in one syscall (MAX_RW_COUNT).
pub export fn zig_user_copy_len_ok(len: u64) c_int {
    const max: u64 = 0x7ffff000;
    return if (len > max) 1 else 0;
}

/// 0 iff a + b does not overflow and (a + b) <= cap.
pub export fn zig_u64_add_le_cap(a: u64, b: u64, cap: u64) c_int {
    const r = @addWithOverflow(a, b);
    if (r[1] != 0) return 1;
    if (r[0] > cap) return 1;
    return 0;
}

/// Power-of-two alignment only; must be non-zero (`align` is reserved in Zig).
pub export fn zig_u64_align_up_pow2_ok(value: u64, alignment: u64, out: *u64) c_int {
    if (@intFromPtr(out) == 0) return 1;
    if (alignment == 0) return 1;
    if ((alignment & (alignment - 1)) != 0) return 1;
    const mask = alignment - 1;
    const r = @addWithOverflow(value, mask);
    if (r[1] != 0) return 1;
    out.* = r[0] & ~mask;
    return 0;
}
