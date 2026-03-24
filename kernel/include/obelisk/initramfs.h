/*
 * Obelisk OS - Initramfs Import Interface
 * From Axioms, Order.
 */

#ifndef _OBELISK_INITRAMFS_H
#define _OBELISK_INITRAMFS_H

#include <obelisk/types.h>

int initramfs_unpack_tar(const void *archive, size_t archive_size);

#endif
