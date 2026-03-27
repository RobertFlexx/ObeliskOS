/*
 * Obelisk OS - Zig Wi-Fi helper exports
 */
#ifndef _OBELISK_ZIG_WIFI_H
#define _OBELISK_ZIG_WIFI_H

#include <obelisk/types.h>

/* Returns 0 if min_size <= size <= max_size, else non-zero. */
int zig_wifi_fw_size_ok(uint64_t size, uint64_t min_size, uint64_t max_size);

/* Returns 0 and writes ceil(total/chunk) to out_count, else non-zero. */
int zig_wifi_chunk_count_ok(uint64_t total, uint64_t chunk, uint64_t *out_count);

#endif /* _OBELISK_ZIG_WIFI_H */
