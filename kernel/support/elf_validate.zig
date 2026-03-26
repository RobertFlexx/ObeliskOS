const Elf64Hdr = extern struct {
    e_ident: [16]u8,
    e_type: u16,
    e_machine: u16,
    e_version: u32,
    e_entry: u64,
    e_phoff: u64,
    e_shoff: u64,
    e_flags: u32,
    e_ehsize: u16,
    e_phentsize: u16,
    e_phnum: u16,
    e_shentsize: u16,
    e_shnum: u16,
    e_shstrndx: u16,
};

const ELF_CLASS_64: u8 = 2;
const ELF_DATA_LSB: u8 = 1;
const ELF_VERSION_CURRENT: u8 = 1;
const ET_EXEC: u16 = 2;
const ET_DYN: u16 = 3;
const EM_X86_64: u16 = 62;

fn mulU64(a: u64, b: u64, out: *u64) bool {
    const r = @mulWithOverflow(a, b);
    out.* = r[0];
    return r[1] == 0;
}

fn addU64(a: u64, b: u64, out: *u64) bool {
    const r = @addWithOverflow(a, b);
    out.* = r[0];
    return r[1] == 0;
}

pub export fn zig_elf64_header_sanity(hdr: ?*const Elf64Hdr, file_size: u64) c_int {
    if (hdr == null) return 1;
    const h = hdr.?;

    if (h.e_ident[0] != 0x7f or h.e_ident[1] != 'E' or h.e_ident[2] != 'L' or h.e_ident[3] != 'F') return 1;
    if (h.e_ident[4] != ELF_CLASS_64) return 1;
    if (h.e_ident[5] != ELF_DATA_LSB) return 1;
    if (h.e_ident[6] != ELF_VERSION_CURRENT) return 1;
    if (h.e_type != ET_EXEC and h.e_type != ET_DYN) return 1;
    if (h.e_machine != EM_X86_64) return 1;

    if (h.e_ehsize != @sizeOf(Elf64Hdr)) return 1;
    if (file_size < @sizeOf(Elf64Hdr)) return 1;

    if (h.e_phnum == 0) return 0;
    if (h.e_phentsize != 56) return 1;

    var ph_bytes: u64 = 0;
    if (!mulU64(@as(u64, h.e_phentsize), @as(u64, h.e_phnum), &ph_bytes)) return 1;

    var ph_end: u64 = 0;
    if (!addU64(h.e_phoff, ph_bytes, &ph_end)) return 1;
    if (ph_end > file_size) return 1;

    return 0;
}
