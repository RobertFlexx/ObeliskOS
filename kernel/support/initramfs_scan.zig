//! In-memory initramfs sanity: cpio newc and POSIX ustar tar (Obelisk ships tar).

const CPIO_MAGIC = "070701";
const TRAILER_NAME = "TRAILER!!!";

fn align4(x: u64) u64 {
    return (x + 3) & ~@as(u64, 3);
}

fn align512(x: u64) u64 {
    return (x + 511) & ~@as(u64, 511);
}

fn addU64(a: u64, b: u64, out: *u64) bool {
    const r = @addWithOverflow(a, b);
    out.* = r[0];
    return r[1] == 0;
}

fn parse_hex8(p: [*]const u8) ?u64 {
    var v: u64 = 0;
    var i: usize = 0;
    while (i < 8) : (i += 1) {
        const c = p[i];
        const d: u4 = switch (c) {
            '0'...'9' => @truncate(c - '0'),
            'a'...'f' => @truncate(10 + (c - 'a')),
            'A'...'F' => @truncate(10 + (c - 'A')),
            else => return null,
        };
        v = (v << 4) | @as(u64, d);
    }
    return v;
}

fn memeq_prefix(p: [*]const u8, needle: []const u8) bool {
    var i: usize = 0;
    while (i < needle.len) : (i += 1) {
        if (p[i] != needle[i]) return false;
    }
    return true;
}

fn is_zero_block(p: [*]const u8) bool {
    var i: usize = 0;
    while (i < 512) : (i += 1) {
        if (p[i] != 0) return false;
    }
    return true;
}

fn parse_octal_field(p: [*]const u8, n: usize) ?u64 {
    var v: u64 = 0;
    var i: usize = 0;
    while (i < n and (p[i] == ' ' or p[i] == 0)) : (i += 1) {}
    while (i < n) : (i += 1) {
        const c = p[i];
        if (c < '0' or c > '7') break;
        const d = @as(u64, c - '0');
        const sh = @shlWithOverflow(v, 3);
        if (sh[1] != 0) return null;
        const add = @addWithOverflow(sh[0], d);
        if (add[1] != 0) return null;
        v = add[0];
    }
    return v;
}

fn scan_cpio_newc(base: [*]const u8, total: u64) c_int {
    var off: u64 = 0;
    while (true) {
        if (total < off) return 1;
        const remain = total - off;
        if (remain < 110) return 1;

        const h = base + @as(usize, off);
        if (!memeq_prefix(h, CPIO_MAGIC)) return 1;

        const filesize = parse_hex8(h + 54) orelse return 1;
        const namesize = parse_hex8(h + 94) orelse return 1;

        if (namesize == 0) return 1;

        var name_end: u64 = undefined;
        if (!addU64(off, 110, &name_end)) return 1;
        if (!addU64(name_end, namesize, &name_end)) return 1;
        if (name_end > total) return 1;

        const name_ptr = base + @as(usize, off + 110);

        const is_trailer = namesize >= TRAILER_NAME.len + 1 and
            memeq_prefix(name_ptr, TRAILER_NAME) and
            name_ptr[TRAILER_NAME.len] == 0;

        const data_start = align4(off + 110 + namesize);
        if (data_start > total) return 1;

        if (is_trailer) {
            if (filesize != 0) return 1;
            return 0;
        }

        var data_end: u64 = undefined;
        if (!addU64(data_start, filesize, &data_end)) return 1;
        if (data_end > total) return 1;

        off = align4(data_end);
    }
}

fn scan_tar_ustar(base: [*]const u8, total: u64) c_int {
    var off: u64 = 0;
    while (true) {
        if (total < off) return 1;
        const remain = total - off;
        if (remain < 512) {
            return if (remain == 0) 0 else 1;
        }
        const p = base + @as(usize, off);
        if (is_zero_block(p)) {
            if (remain >= 1024 and !is_zero_block(p + 512)) return 1;
            return 0;
        }
        // Match initramfs_unpack_tar: empty name ends the archive (not an error).
        if (p[0] == 0) {
            return 0;
        }
        const fsize = parse_octal_field(p + 124, 12) orelse return 1;
        var adv: u64 = undefined;
        if (!addU64(512, align512(fsize), &adv)) return 1;
        var next: u64 = undefined;
        if (!addU64(off, adv, &next)) return 1;
        if (next > total) return 1;
        off = next;
    }
}

pub export fn zig_initramfs_scan(base: ?*const anyopaque, size: u64) c_int {
    if (base == null) return 1;
    if (size == 0) return 1;
    const b: [*]const u8 = @ptrCast(base.?);

    if (size >= 6 and memeq_prefix(b, CPIO_MAGIC)) {
        return scan_cpio_newc(b, size);
    }

    if (size >= 512) {
        return scan_tar_ustar(b, size);
    }

    return 1;
}
