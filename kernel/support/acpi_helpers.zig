//! ACPI helper routines exported to C kernel code.

const MAX_ACPI_SCAN: u64 = 16 * 1024 * 1024;

pub export fn zig_acpi_checksum_ok(buf: ?*const anyopaque, len: u64) c_int {
    if (buf == null or len == 0) return 0;
    if (len > MAX_ACPI_SCAN) return 0;
    const bytes: [*]const u8 = @ptrCast(@alignCast(buf.?));
    var sum: u8 = 0;
    var i: u64 = 0;
    while (i < len) : (i += 1) {
        sum +%= bytes[i];
    }
    return if (sum == 0) 1 else 0;
}

pub export fn zig_find_s5_name_offset(buf: ?*const anyopaque, len: u64) i64 {
    if (buf == null or len < 4) return -1;
    if (len > MAX_ACPI_SCAN) return -1;
    const bytes: [*]const u8 = @ptrCast(@alignCast(buf.?));
    var i: u64 = 0;
    while (i + 4 <= len) : (i += 1) {
        if (bytes[i] == '_' and bytes[i + 1] == 'S' and bytes[i + 2] == '5' and bytes[i + 3] == '_') {
            return @intCast(i);
        }
    }
    return -1;
}
