/*
 * Obelisk OS - Exec argv/env string policy (Zig)
 * From Axioms, Order.
 *
 * After copy_user_cstring fills a kernel buffer: non-empty, NUL within cap,
 * reject C0 except TAB, reject DEL. Allows '=' for environment KEY=value lines.
 */

#ifndef _OBELISK_ZIG_EXEC_STRING_H
#define _OBELISK_ZIG_EXEC_STRING_H

#include <obelisk/types.h>

int zig_kernel_exec_line_ok(const char *s, uint64_t cap);

/* Returns index of first NUL in [0, cap), or -1 if unterminated in range. */
int64_t zig_cstring_first_nul_index(const char *buf, uint64_t cap);

#endif
