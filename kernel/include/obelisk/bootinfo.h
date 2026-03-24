/*
 * Obelisk OS - Boot Information Interface
 * From Axioms, Order.
 */

#ifndef _OBELISK_BOOTINFO_H
#define _OBELISK_BOOTINFO_H

#include <obelisk/types.h>

#define OBELISK_BOOT_MAX_MODULES 16
#define OBELISK_BOOT_MODULE_NAME_LEN 64

struct obelisk_boot_module {
    uint64_t start;
    uint64_t end;
    char name[OBELISK_BOOT_MODULE_NAME_LEN];
};

void bootinfo_init(uint32_t magic, uint64_t multiboot_addr);
const char *bootinfo_cmdline(void);
size_t bootinfo_module_count(void);
const struct obelisk_boot_module *bootinfo_module_at(size_t index);
const struct obelisk_boot_module *bootinfo_find_module(const char *name);

#endif
