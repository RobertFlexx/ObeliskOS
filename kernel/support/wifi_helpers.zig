//! Zig helpers for Wi-Fi firmware/bootstrap math.

const std = @import("std");

pub export fn zig_wifi_fw_size_ok(size: u64, min_size: u64, max_size: u64) c_int {
    if (min_size == 0 or max_size == 0 or min_size > max_size) return 1;
    if (size < min_size or size > max_size) return 1;
    return 0;
}

pub export fn zig_wifi_chunk_count_ok(total: u64, chunk: u64, out_count: *u64) c_int {
    if (@intFromPtr(out_count) == 0) return 1;
    if (chunk == 0) return 1;
    if (total == 0) {
        out_count.* = 0;
        return 0;
    }
    const rem = total % chunk;
    const div = total / chunk;
    if (rem == 0) {
        out_count.* = div;
        return 0;
    }
    if (div == std.math.maxInt(u64)) return 1;
    out_count.* = div + 1;
    return 0;
}
