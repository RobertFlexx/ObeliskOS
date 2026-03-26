//! 64-bit multiply with overflow detection for kernel C callers.

pub export fn zig_u64_mul_ok(a: u64, b: u64, out: *u64) c_int {
    const r = @mulWithOverflow(a, b);
    out.* = r[0];
    return if (r[1] != 0) 1 else 0;
}
