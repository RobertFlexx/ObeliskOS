/*
 * Obelisk OS - Zig overflow-checked u64 arithmetic (C ABI)
 * From Axioms, Order.
 *
 * All functions return 0 on success (*out = result), non-zero on overflow/underflow.
 */

#ifndef _OBELISK_ZIG_SAFE_ARITH_H
#define _OBELISK_ZIG_SAFE_ARITH_H

#include <obelisk/types.h>

int zig_u64_mul_ok(uint64_t a, uint64_t b, uint64_t *out);
int zig_u64_add_ok(uint64_t a, uint64_t b, uint64_t *out);
int zig_u64_sub_ok(uint64_t a, uint64_t b, uint64_t *out);

/* Linux MAX_RW_COUNT-style cap for user buffer transfers (read/write/getrandom/...). */
int zig_user_copy_len_ok(uint64_t len);

/* 0 iff a+b <= cap and a+b does not overflow. */
int zig_u64_add_le_cap(uint64_t a, uint64_t b, uint64_t cap);

/* ALIGN_UP for power-of-two align only. */
int zig_u64_align_up_pow2_ok(uint64_t value, uint64_t align, uint64_t *out);

#endif
