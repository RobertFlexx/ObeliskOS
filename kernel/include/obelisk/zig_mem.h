/*
 * Obelisk OS - Zig memory/range helper exports
 * From Axioms, Order.
 */

#ifndef _OBELISK_ZIG_MEM_H
#define _OBELISK_ZIG_MEM_H

#include <obelisk/types.h>

/*
 * Computes:
 *   aligned = align_up_pow2(bytes, page_size)
 *   pages   = aligned / page_size
 * Returns 0 on success, non-zero on invalid input/overflow.
 */
int zig_pages_for_bytes_ok(uint64_t bytes, uint64_t page_size,
                           uint64_t *aligned_out, uint64_t *pages_out);

/*
 * Validates half-open range [start, start+length) with overflow guard and cap.
 * Writes end = start + length on success.
 * Returns 0 on success, non-zero on invalid input/overflow/out-of-bounds.
 */
int zig_range_end_le_cap_ok(uint64_t start, uint64_t length, uint64_t cap, uint64_t *end_out);

#endif /* _OBELISK_ZIG_MEM_H */

