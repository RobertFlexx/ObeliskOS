/*
 * Obelisk OS - Wi-Fi probe/compat matrix
 * From Axioms, Order.
 *
 * This module intentionally focuses on robust hardware detection and clear
 * diagnostics for bare-metal bring-up. Full 802.11 MAC/firmware datapaths
 * are staged separately.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <drivers/pci.h>
#include <net/wifi_probe.h>

#define WIFI_VENDOR_INTEL    0x8086
#define WIFI_VENDOR_ATHEROS  0x168C
#define WIFI_VENDOR_REALTEK  0x10EC
#define WIFI_VENDOR_BROADCOM 0x14E4

struct wifi_id {
    uint16_t vendor;
    uint16_t device;
    const char *family;
    const char *note;
};

/*
 * Curated set of common desktop/laptop Wi-Fi chips we can identify early.
 * "note" advertises intended next driver family.
 */
static const struct wifi_id g_wifi_ids[] = {
    { WIFI_VENDOR_INTEL,    0x0082, "Intel Centrino 6205", "iwlwifi-class" },
    { WIFI_VENDOR_INTEL,    0x24FD, "Intel 7265",          "iwlwifi-class" },
    { WIFI_VENDOR_INTEL,    0x2723, "Intel AX200",         "iwlwifi-class" },
    { WIFI_VENDOR_INTEL,    0x7AF0, "Intel AX210",         "iwlwifi-class" },
    { WIFI_VENDOR_ATHEROS,  0x0030, "Atheros AR93xx",      "ath9k-class" },
    { WIFI_VENDOR_ATHEROS,  0x0032, "Atheros QCA95xx",     "ath9k-class" },
    { WIFI_VENDOR_REALTEK,  0x8176, "Realtek RTL8188CE",   "rtlwifi-class" },
    { WIFI_VENDOR_REALTEK,  0x818B, "Realtek RTL8192EE",   "rtlwifi-class" },
    { WIFI_VENDOR_REALTEK,  0xB852, "Realtek 8852BE",      "rtw89-class" },
    { WIFI_VENDOR_BROADCOM, 0x43A0, "Broadcom BCM4360",    "brcmfmac-class" },
    { WIFI_VENDOR_BROADCOM, 0x43B1, "Broadcom BCM4352",    "brcmfmac-class" },
    { WIFI_VENDOR_BROADCOM, 0x43C3, "Broadcom BCM43602",   "brcmfmac-class" },
};

static const struct wifi_id *wifi_lookup_id(uint16_t vendor, uint16_t device) {
    for (size_t i = 0; i < sizeof(g_wifi_ids) / sizeof(g_wifi_ids[0]); i++) {
        if (g_wifi_ids[i].vendor == vendor && g_wifi_ids[i].device == device) {
            return &g_wifi_ids[i];
        }
    }
    return NULL;
}

void wifi_probe_init(void) {
    size_t count = pci_device_count();
    size_t wifi_seen = 0;
    size_t known_seen = 0;

    printk(KERN_INFO "wifi: probing PCI Wi-Fi controllers...\n");
    for (size_t i = 0; i < count; i++) {
        const struct pci_device *d = pci_get_device(i);
        const struct wifi_id *id;
        if (!d) {
            continue;
        }
        if (d->class_code != 0x02 || d->subclass != 0x80) {
            continue;
        }
        wifi_seen++;
        id = wifi_lookup_id(d->vendor_id, d->device_id);
        if (id) {
            known_seen++;
            printk(KERN_INFO "wifi: %s detected at %02x:%02x.%u (%04x:%04x, target=%s)\n",
                   id->family, d->bus, d->slot, d->function,
                   d->vendor_id, d->device_id, id->note);
        } else {
            printk(KERN_WARNING "wifi: unknown controller at %02x:%02x.%u (%04x:%04x) — add driver profile\n",
                   d->bus, d->slot, d->function, d->vendor_id, d->device_id);
        }
    }
        // i fucking hate writing driver software smfh
    if (wifi_seen == 0) {
        printk(KERN_INFO "wifi: no PCI Wi-Fi controller found\n");
        return;
    }
    if (known_seen == 0) {
        printk(KERN_WARNING "wifi: controller(s) detected but none in current compatibility matrix\n");
        return;
    }
    printk(KERN_NOTICE "wifi: %lu/%lu controller(s) recognized; datapath enablement follows by driver family\n",
           (uint64_t)known_seen, (uint64_t)wifi_seen);
}
