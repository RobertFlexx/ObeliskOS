/*
 * Obelisk OS - Zig initramfs archive sanity (C ABI)
 * From Axioms, Order.
 */

#ifndef _OBELISK_ZIG_INITRAMFS_H
#define _OBELISK_ZIG_INITRAMFS_H

#include <obelisk/types.h>

/*
 * Validate an in-memory initramfs image before unpack.
 * Returns 0 if the archive is structurally sound (cpio "newc" or POSIX ustar tar).
 * Returns non-zero if truncated, malformed, or unknown format.
 */
int zig_initramfs_scan(const void *base, uint64_t size);

#endif
