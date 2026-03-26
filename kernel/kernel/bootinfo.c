/*
 * Obelisk OS - Multiboot2 Boot Metadata Parser
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <obelisk/bootinfo.h>

#define MULTIBOOT2_MAGIC 0x36d76289
#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_CMDLINE 1
#define MULTIBOOT_TAG_TYPE_MODULE 3
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8
#define MULTIBOOT_TAG_TYPE_ACPI_OLD 14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW 15

#define BOOTINFO_ACPI_RSDP_MAX 36

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
} __packed;

struct multiboot_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
} __packed;

struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
} __packed;

struct multiboot_tag_framebuffer_common {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} __packed;

static uint8_t boot_acpi_rsdp[BOOTINFO_ACPI_RSDP_MAX];
static size_t boot_acpi_rsdp_len;

static char boot_cmdline[256];
static struct obelisk_boot_module boot_modules[OBELISK_BOOT_MAX_MODULES];
static size_t boot_module_count;
static struct obelisk_framebuffer_info boot_fb;

static void copy_module_name(char *dst, size_t dst_len, const char *src) {
    size_t i = 0;
    while (i + 1 < dst_len && src[i] && src[i] != ' ') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void bootinfo_init(uint32_t magic, uint64_t multiboot_addr) {
    struct multiboot_tag *tag;

    boot_cmdline[0] = '\0';
    boot_module_count = 0;
    boot_acpi_rsdp_len = 0;
    memset(&boot_fb, 0, sizeof(boot_fb));

    if (magic != MULTIBOOT2_MAGIC) {
        printk(KERN_WARNING "bootinfo: invalid multiboot magic: 0x%x\n", magic);
        return;
    }

    tag = (struct multiboot_tag *)(multiboot_addr + 8);
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_CMDLINE) {
            struct multiboot_tag_string *cmd = (struct multiboot_tag_string *)tag;
            strncpy(boot_cmdline, cmd->string, sizeof(boot_cmdline) - 1);
            boot_cmdline[sizeof(boot_cmdline) - 1] = '\0';
        } else if (tag->type == MULTIBOOT_TAG_TYPE_MODULE &&
                   boot_module_count < OBELISK_BOOT_MAX_MODULES) {
            struct multiboot_tag_module *mod = (struct multiboot_tag_module *)tag;
            struct obelisk_boot_module *dst = &boot_modules[boot_module_count++];
            dst->start = mod->mod_start;
            dst->end = mod->mod_end;
            copy_module_name(dst->name, sizeof(dst->name), mod->cmdline);
        } else if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            struct multiboot_tag_framebuffer_common *fb = (struct multiboot_tag_framebuffer_common *)tag;
            boot_fb.phys_addr = fb->framebuffer_addr;
            boot_fb.pitch = fb->framebuffer_pitch;
            boot_fb.width = fb->framebuffer_width;
            boot_fb.height = fb->framebuffer_height;
            boot_fb.bpp = fb->framebuffer_bpp;
            boot_fb.type = fb->framebuffer_type;
            boot_fb.available = true;
        } else if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_OLD ||
                   tag->type == MULTIBOOT_TAG_TYPE_ACPI_NEW) {
            size_t payload = tag->size;
            if (payload > sizeof(struct multiboot_tag)) {
                payload -= sizeof(struct multiboot_tag);
                if (payload > BOOTINFO_ACPI_RSDP_MAX) {
                    payload = BOOTINFO_ACPI_RSDP_MAX;
                }
                /* Prefer ACPI 2.0+ tag; accept legacy only if nothing else was seen. */
                if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_NEW || boot_acpi_rsdp_len == 0) {
                    memcpy(boot_acpi_rsdp, (const uint8_t *)tag + sizeof(struct multiboot_tag), payload);
                    boot_acpi_rsdp_len = payload;
                }
            }
        }

        tag = (struct multiboot_tag *)ALIGN_UP((uint64_t)tag + tag->size, 8);
    }

    printk(KERN_INFO "bootinfo: cmdline='%s'\n", boot_cmdline[0] ? boot_cmdline : "(none)");
    printk(KERN_INFO "bootinfo: discovered %lu module(s)\n", boot_module_count);
    for (size_t i = 0; i < boot_module_count; i++) {
        printk(KERN_INFO "bootinfo: module[%lu] %s @ [0x%lx..0x%lx)\n",
               i, boot_modules[i].name, boot_modules[i].start, boot_modules[i].end);
    }
    if (boot_fb.available) {
        printk(KERN_INFO "bootinfo: framebuffer %lux%lu pitch=%lu bpp=%u phys=0x%lx\n",
               (unsigned long)boot_fb.width,
               (unsigned long)boot_fb.height,
               (unsigned long)boot_fb.pitch,
               (unsigned int)boot_fb.bpp,
               boot_fb.phys_addr);
    } else {
        printk(KERN_INFO "bootinfo: framebuffer unavailable\n");
    }
    if (boot_acpi_rsdp_len > 0) {
        printk(KERN_INFO "bootinfo: ACPI RSDP tag present (%lu bytes, sig %.8s)\n",
               (unsigned long)boot_acpi_rsdp_len, (const char *)boot_acpi_rsdp);
    } else {
        printk(KERN_INFO "bootinfo: ACPI RSDP tag not provided by bootloader\n");
    }
}

const char *bootinfo_cmdline(void) {
    return boot_cmdline;
}

size_t bootinfo_module_count(void) {
    return boot_module_count;
}

const struct obelisk_boot_module *bootinfo_module_at(size_t index) {
    if (index >= boot_module_count) {
        return NULL;
    }
    return &boot_modules[index];
}

const struct obelisk_boot_module *bootinfo_find_module(const char *name) {
    if (!name || !*name) {
        return NULL;
    }
    for (size_t i = 0; i < boot_module_count; i++) {
        if (strcmp(boot_modules[i].name, name) == 0) {
            return &boot_modules[i];
        }
    }
    return NULL;
}

const struct obelisk_framebuffer_info *bootinfo_framebuffer(void) {
    return &boot_fb;
}

const uint8_t *bootinfo_acpi_rsdp(void) {
    return boot_acpi_rsdp_len > 0 ? boot_acpi_rsdp : NULL;
}

size_t bootinfo_acpi_rsdp_len(void) {
    return boot_acpi_rsdp_len;
}
