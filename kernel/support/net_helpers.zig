//! Generic network data-path helper checks for C drivers.

pub export fn zig_net_mac_is_valid(mac: ?[*]const u8, len: u64) c_int {
    if (mac == null or len != 6) return 0;
    const p = mac.?;
    var all_zero = true;
    var all_ff = true;
    var i: u64 = 0;
    while (i < 6) : (i += 1) {
        const b = p[@intCast(i)];
        if (b != 0) all_zero = false;
        if (b != 0xff) all_ff = false;
    }
    if (all_zero or all_ff) return 0;
    if ((p[0] & 0x01) != 0) return 0;
    return 1;
}

pub export fn zig_net_frame_len_ok(len: u64, min_len: u64, max_len: u64) c_int {
    if (min_len == 0 or max_len == 0 or min_len > max_len) return 1;
    if (len < min_len or len > max_len) return 1;
    return 0;
}

pub export fn zig_net_ring_bytes_ok(entries: u64, desc_size: u64, alignment: u64, max_bytes: u64, out_bytes: *u64) c_int {
    if (@intFromPtr(out_bytes) == 0) return 1;
    if (entries == 0 or desc_size == 0 or alignment == 0) return 1;
    if ((alignment & (alignment - 1)) != 0) return 1;
    if (max_bytes == 0) return 1;

    const mul = @mulWithOverflow(entries, desc_size);
    if (mul[1] != 0) return 1;
    const raw = mul[0];
    const mask = alignment - 1;
    const add = @addWithOverflow(raw, mask);
    if (add[1] != 0) return 1;
    const aligned = add[0] & ~mask;
    if (aligned == 0 or aligned > max_bytes) return 1;
    out_bytes.* = aligned;
    return 0;
}
