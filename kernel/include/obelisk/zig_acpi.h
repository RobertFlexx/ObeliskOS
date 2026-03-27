/*
 * Obelisk OS - Zig ACPI helper exports
 */
#ifndef _OBELISK_ZIG_ACPI_H
#define _OBELISK_ZIG_ACPI_H

#include <obelisk/types.h>

int zig_acpi_checksum_ok(const void *buf, uint64_t len);
int64_t zig_find_s5_name_offset(const void *buf, uint64_t len);

#endif /* _OBELISK_ZIG_ACPI_H */
