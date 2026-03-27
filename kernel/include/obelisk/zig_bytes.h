/*
 * Obelisk OS - Zig byte/string helpers
 * From Axioms, Order.
 */

#ifndef _OBELISK_ZIG_BYTES_H
#define _OBELISK_ZIG_BYTES_H

#include <obelisk/types.h>

/* Returns 1 if [buf, buf+len) is all zeros, else 0. */
int zig_mem_all_zero(const void *buf, uint64_t len);

/* Returns first zero-byte index in [buf, buf+len), else -1. */
int64_t zig_first_zero_byte(const void *buf, uint64_t len);

/*
 * Returns 1 iff the C-string contains a ".." path component.
 * cap is the max bytes to inspect (must include terminating NUL).
 */
int zig_path_has_dotdot_component(const char *s, uint64_t cap);

#endif /* _OBELISK_ZIG_BYTES_H */

