/*
 * Obelisk OS - Zig network helper exports
 */
#ifndef _OBELISK_ZIG_NET_H
#define _OBELISK_ZIG_NET_H

#include <obelisk/types.h>

/* Returns 1 for valid unicast MAC (len must be 6), else 0. */
int zig_net_mac_is_valid(const uint8_t *mac, uint64_t len);

/* Returns 0 when min_len <= len <= max_len, else non-zero. */
int zig_net_frame_len_ok(uint64_t len, uint64_t min_len, uint64_t max_len);

/*
 * Computes aligned bytes for ring descriptors and validates a configured cap.
 * Returns 0 on success and writes to out_bytes, else non-zero.
 */
int zig_net_ring_bytes_ok(uint64_t entries, uint64_t desc_size, uint64_t align,
                          uint64_t max_bytes, uint64_t *out_bytes);

#endif /* _OBELISK_ZIG_NET_H */
