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

struct obelisk_framebuffer_info {
    uint64_t phys_addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t type;
    bool available;
};

void bootinfo_init(uint32_t magic, uint64_t multiboot_addr);
const char *bootinfo_cmdline(void);
size_t bootinfo_module_count(void);
const struct obelisk_boot_module *bootinfo_module_at(size_t index);
const struct obelisk_boot_module *bootinfo_find_module(const char *name);
const struct obelisk_framebuffer_info *bootinfo_framebuffer(void);

/* ACPI RSDP copy from multiboot2 tag (if present); len 0 if unavailable. */
const uint8_t *bootinfo_acpi_rsdp(void);
size_t bootinfo_acpi_rsdp_len(void);

#endif
