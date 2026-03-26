/*
 * Obelisk OS - Kernel power / reset
 * From Axioms, Order.
 */

#ifndef _OBELISK_POWER_H
#define _OBELISK_POWER_H

void kernel_poweroff_hardware(void) __attribute__((noreturn));
void kernel_reboot_hardware(void) __attribute__((noreturn));

#endif
