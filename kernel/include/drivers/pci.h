/*
 * Obelisk OS - PCI Bus Support
 * From Axioms, Order.
 */

#ifndef _DRIVERS_PCI_H
#define _DRIVERS_PCI_H

#include <obelisk/types.h>

#define PCI_MAX_DEVICES 128

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint32_t bar[6];
};

void pci_init(void);
size_t pci_device_count(void);
const struct pci_device *pci_get_device(size_t index);

/* Raw PCI config accessors (Type 1 / CF8-CFC). */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint8_t value);

#endif /* _DRIVERS_PCI_H */
