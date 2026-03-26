/*
 * Obelisk OS - 64-bit multiply overflow check (Zig)
 * From Axioms, Order.
 */

#ifndef _OBELISK_ZIG_MUL_H
#define _OBELISK_ZIG_MUL_H

#include <obelisk/types.h>

/* Returns 0 on success (*out = a*b), non-zero on overflow. */
int zig_u64_mul_ok(uint64_t a, uint64_t b, uint64_t *out);

#endif
