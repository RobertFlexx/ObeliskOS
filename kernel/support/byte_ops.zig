//! Byte scanning and path component helpers for kernel C code.

pub export fn zig_mem_all_zero(buf: ?*const anyopaque, len: u64) c_int {
    if (buf == null) return 0;
    const p: [*]const u8 = @ptrCast(buf.?);
    var i: u64 = 0;
    while (i < len) : (i += 1) {
        if (p[@intCast(i)] != 0) return 0;
    }
    return 1;
}

pub export fn zig_first_zero_byte(buf: ?*const anyopaque, len: u64) i64 {
    if (buf == null) return -1;
    const p: [*]const u8 = @ptrCast(buf.?);
    var i: u64 = 0;
    while (i < len) : (i += 1) {
        if (p[@intCast(i)] == 0) return @intCast(i);
    }
    return -1;
}

pub export fn zig_path_has_dotdot_component(s: ?[*]const u8, cap: u64) c_int {
    if (s == null or cap == 0) return 0;

    const p = s.?;
    var i: u64 = 0;
    var seg_len: u64 = 0;

    while (i < cap) : (i += 1) {
        const c = p[@intCast(i)];
        if (c == 0) {
            if (seg_len == 2 and p[@intCast(i - 2)] == '.' and p[@intCast(i - 1)] == '.') {
                return 1;
            }
            return 0;
        }
        if (c == '/') {
            if (seg_len == 2 and p[@intCast(i - 2)] == '.' and p[@intCast(i - 1)] == '.') {
                return 1;
            }
            seg_len = 0;
            continue;
        }
        seg_len += 1;
    }
    if (seg_len == 2 and cap >= 2 and p[@intCast(cap - 2)] == '.' and p[@intCast(cap - 1)] == '.') {
        return 1;
    }
    return 0;
}

