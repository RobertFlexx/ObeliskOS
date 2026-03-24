/*
 * Obelisk OS - PCI Bus Support
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <drivers/pci.h>

#define PCI_CONFIG_ADDR_PORT 0xCF8
#define PCI_CONFIG_DATA_PORT 0xCFC
#define PCI_INVALID_VENDOR_ID 0xFFFF

static struct pci_device pci_devices[PCI_MAX_DEVICES];
static size_t pci_device_used;

static uint32_t pci_make_cfg_addr(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return BIT(31) |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           (offset & 0xFCU);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDR_PORT, pci_make_cfg_addr(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA_PORT);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t v = pci_config_read32(bus, slot, function, offset);
    uint8_t shift = (offset & 0x2U) * 8U;
    return (uint16_t)((v >> shift) & 0xFFFFU);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t v = pci_config_read32(bus, slot, function, offset);
    uint8_t shift = (offset & 0x3U) * 8U;
    return (uint8_t)((v >> shift) & 0xFFU);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDR_PORT, pci_make_cfg_addr(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA_PORT, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t v = pci_config_read32(bus, slot, function, offset);
    uint8_t shift = (offset & 0x2U) * 8U;
    v &= ~(0xFFFFU << shift);
    v |= (uint32_t)value << shift;
    pci_config_write32(bus, slot, function, offset, v);
}

void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t v = pci_config_read32(bus, slot, function, offset);
    uint8_t shift = (offset & 0x3U) * 8U;
    v &= ~(0xFFU << shift);
    v |= (uint32_t)value << shift;
    pci_config_write32(bus, slot, function, offset, v);
}

static void pci_capture_device(uint8_t bus, uint8_t slot, uint8_t function) {
    struct pci_device *d;

    if (pci_device_used >= PCI_MAX_DEVICES) {
        return;
    }

    d = &pci_devices[pci_device_used];
    memset(d, 0, sizeof(*d));

    d->bus = bus;
    d->slot = slot;
    d->function = function;
    d->vendor_id = pci_config_read16(bus, slot, function, 0x00);
    d->device_id = pci_config_read16(bus, slot, function, 0x02);
    d->revision = pci_config_read8(bus, slot, function, 0x08);
    d->prog_if = pci_config_read8(bus, slot, function, 0x09);
    d->subclass = pci_config_read8(bus, slot, function, 0x0A);
    d->class_code = pci_config_read8(bus, slot, function, 0x0B);
    d->header_type = pci_config_read8(bus, slot, function, 0x0E);
    d->interrupt_line = pci_config_read8(bus, slot, function, 0x3C);
    d->interrupt_pin = pci_config_read8(bus, slot, function, 0x3D);

    for (int i = 0; i < 6; i++) {
        d->bar[i] = pci_config_read32(bus, slot, function, (uint8_t)(0x10 + (i * 4)));
    }

    pci_device_used++;
}

void pci_init(void) {
    pci_device_used = 0;
    printk(KERN_INFO "PCI: scanning bus topology...\n");

    for (uint16_t bus = 0; bus <= 255; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_config_read16((uint8_t)bus, slot, 0, 0x00);
            uint8_t header_type;
            uint8_t function_count;

            if (vendor == PCI_INVALID_VENDOR_ID) {
                continue;
            }

            header_type = pci_config_read8((uint8_t)bus, slot, 0, 0x0E);
            function_count = (header_type & 0x80U) ? 8 : 1;
            for (uint8_t function = 0; function < function_count; function++) {
                vendor = pci_config_read16((uint8_t)bus, slot, function, 0x00);
                if (vendor == PCI_INVALID_VENDOR_ID) {
                    continue;
                }
                pci_capture_device((uint8_t)bus, slot, function);
            }
        }
    }

    printk(KERN_INFO "PCI: %lu device function(s) discovered\n", (uint64_t)pci_device_used);
    for (size_t i = 0; i < pci_device_used; i++) {
        const struct pci_device *d = &pci_devices[i];
        if (d->class_code == 0x02 || d->vendor_id == 0x1AF4) {
            printk(KERN_INFO
                   "PCI: %02x:%02x.%u vendor=%04x device=%04x class=%02x:%02x irq=%u\n",
                   d->bus, d->slot, d->function,
                   d->vendor_id, d->device_id,
                   d->class_code, d->subclass,
                   d->interrupt_line);
        }
    }
}

size_t pci_device_count(void) {
    return pci_device_used;
}

const struct pci_device *pci_get_device(size_t index) {
    if (index >= pci_device_used) {
        return NULL;
    }
    return &pci_devices[index];
}
