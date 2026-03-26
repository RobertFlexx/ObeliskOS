#ifndef _OBELISK_ZIG_ELF_H
#define _OBELISK_ZIG_ELF_H

#include <obelisk/types.h>

struct elf64_hdr;

/*
 * Returns 0 when header/program-header table layout is sane, non-zero otherwise.
 * This helper is intentionally strict and side-effect free.
 */
int zig_elf64_header_sanity(const struct elf64_hdr *hdr, uint64_t file_size);

#endif /* _OBELISK_ZIG_ELF_H */
