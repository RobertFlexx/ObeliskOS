//! Zig memory/range helpers exported to C kernel code.

pub export fn zig_pages_for_bytes_ok(bytes: u64, page_size: u64, aligned_out: *u64, pages_out: *u64) c_int {
    if (@intFromPtr(aligned_out) == 0 or @intFromPtr(pages_out) == 0) return 1;
    if (page_size == 0) return 1;
    if ((page_size & (page_size - 1)) != 0) return 1; // pow2 only

    if (bytes == 0) {
        aligned_out.* = 0;
        pages_out.* = 0;
        return 0;
    }

    const mask = page_size - 1;
    const add = @addWithOverflow(bytes, mask);
    if (add[1] != 0) return 1;
    const aligned = add[0] & ~mask;
    if (aligned < bytes) return 1;
    if (aligned % page_size != 0) return 1;

    aligned_out.* = aligned;
    pages_out.* = aligned / page_size;
    return 0;
}

pub export fn zig_range_end_le_cap_ok(start: u64, length: u64, cap: u64, end_out: *u64) c_int {
    if (@intFromPtr(end_out) == 0) return 1;
    const add = @addWithOverflow(start, length);
    if (add[1] != 0) return 1;
    const end = add[0];
    if (end > cap) return 1;
    end_out.* = end;
    return 0;
}

