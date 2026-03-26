/*
 * Obelisk OS - Kernel power-off and reset
 * From Axioms, Order.
 *
 * Bare metal often lacks QEMU-style PM ports; we still emit a full printk trace
 * so shutdown is visible on serial and framebuffer before halting.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <obelisk/bootinfo.h>
#include <arch/cpu.h>

static void kernel_halt_forever(void) __attribute__((noreturn));
static void kernel_halt_forever(void) {
    for (;;) {
        cli();
        hlt();
    }
}

void kernel_poweroff_hardware(void) {
    const uint8_t *rsdp;
    size_t rsdplen;

    printk(KERN_NOTICE "\n=== Obelisk kernel shutdown sequence ===\n");
    printk(KERN_INFO "poweroff: path=sys_reboot (Linux magic verified)\n");

    rsdp = bootinfo_acpi_rsdp();
    rsdplen = bootinfo_acpi_rsdp_len();
    if (rsdp && rsdplen >= 8) {
        printk(KERN_INFO "poweroff: ACPI RSDP from bootloader (sig %.8s, %lu bytes)\n",
               (const char *)rsdp, (unsigned long)rsdplen);
        printk(KERN_NOTICE "poweroff: ACPI S5 / PM1 sleep control is not implemented; "
                           "using legacy/emulated shutdown ports only.\n");
    } else {
        printk(KERN_WARNING "poweroff: no ACPI RSDP tag from bootloader — ACPI-driven "
                            "soft-off unavailable.\n");
    }

    printk(KERN_INFO "poweroff: block-device sync is not wired in this path (no global flush).\n");
    printk(KERN_INFO "poweroff: trying emulated shutdown ports (QEMU/Bochs-style)\n");
    printk(KERN_INFO "poweroff: [1/3] outw(0x604, 0x2000)\n");
    outw(0x604, 0x2000);
    printk(KERN_INFO "poweroff: [2/3] outw(0xB004, 0x2000)\n");
    outw(0xB004, 0x2000);
    printk(KERN_INFO "poweroff: [3/3] outw(0x4004, 0x3400)\n");
    outw(0x4004, 0x3400);

    printk(KERN_WARNING "poweroff: ports did not stop CPU — disabling interrupts and halting.\n");
    printk(KERN_NOTICE "poweroff: safe to power off or reset manually if the machine stays on.\n");
    cli();
    kernel_halt_forever();
}

void kernel_reboot_hardware(void) {
    int i;

    printk(KERN_NOTICE "\n=== Obelisk kernel reboot sequence ===\n");
    printk(KERN_INFO "reboot: path=sys_reboot (Linux magic verified)\n");
    printk(KERN_INFO "reboot: waiting for PS/2 controller (8042) input buffer @ port 0x64\n");

    for (i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
        pause();
    }

    printk(KERN_INFO "reboot: sending CPU reset pulse: outb(0x64, 0xFE)\n");
    cli();
    outb(0x64, 0xFE);

    printk(KERN_WARNING "reboot: reset did not occur — halting with interrupts off.\n");
    kernel_halt_forever();
}
