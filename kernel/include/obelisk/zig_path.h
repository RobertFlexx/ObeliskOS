/*
 * Obelisk OS - Pathname / sysctl input policy (Zig implementation, C ABI)
 * From Axioms, Order.
 *
 * This is explicit Obelisk policy for user-supplied path-like strings and sysctl
 * names once they have been copied into kernel buffers — not a generic “string
 * validator”. It gates syscall hot paths before VFS, exec, or sysctl dispatch.
 *
 * Policy (see support/path_validate.zig):
 * - Non-empty; NUL must appear within the buffer capacity (bounded scan).
 * - Reject C0 controls (0x01–0x1f) and DEL (0x7f) before the terminator.
 * - Allow space (0x20) and bytes >= 0x80.
 *
 * Returns 0 if the string satisfies policy, non-zero otherwise (callers use -EINVAL).
 */

#ifndef _OBELISK_ZIG_PATH_H
#define _OBELISK_ZIG_PATH_H

#include <obelisk/types.h>

int zig_kernel_cstring_no_control(const char *s, uint64_t cap);

#endif /* _OBELISK_ZIG_PATH_H */
