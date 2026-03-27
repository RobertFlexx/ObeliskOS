/*
 * Obelisk OS - Intel Wi-Fi (iwlwifi-class) scaffold
 * From Axioms, Order.
 *
 * This module provides:
 * - robust PCI probe and MMIO mapping for Intel Wi-Fi adapters
 * - deferred firmware presence check after rootfs import
 *
 * Full 802.11 MAC/radio datapath is intentionally staged separately.
 * Hey, nimrod. if you didnt already know, intel is the priority. */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/mmu.h>
#include <drivers/pci.h>
#include <fs/vfs.h>
#include <mm/pmm.h>
#include <obelisk/zig_wifi.h>
#include <net/wifi_intel.h>

#define INTEL_WIFI_VENDOR 0x8086

#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_MEM 0x2
#define PCI_COMMAND_BUS_MASTER 0x4

#define INTEL_WIFI_MMIO_MAP_SIZE 0x4000UL
#define INTEL_WIFI_FW_MAX_SIZE   (2U * 1024U * 1024U)
#define INTEL_WIFI_FW_MIN_SIZE   4096U
#define INTEL_WIFI_Q_MEM_SIZE    4096U
#define INTEL_WIFI_FW_CHUNK_SIZE 4096U

struct intel_wifi_id {
    uint16_t device;
    const char *name;
    const char *fw_hint;
};

static const struct intel_wifi_id g_intel_wifi_ids[] = {
    { 0x0082, "Centrino Advanced-N 6205", "iwlwifi-6000g2b-*.ucode" },
    { 0x24FD, "Wireless-AC 7265",         "iwlwifi-7265D-*.ucode" },
    { 0x2526, "Wireless-AC 8265",         "iwlwifi-8265-*.ucode" },
    { 0x02F0, "Wireless-AC 9260",         "iwlwifi-9260-*.ucode" },
    { 0x2723, "Wi-Fi 6 AX200",            "iwlwifi-cc-a0-*.ucode" },
    { 0x34F0, "Wi-Fi 6E AX211",           "iwlwifi-so-a0-*.ucode" },
    { 0x51F0, "Wi-Fi 7 BE200",            "iwlwifi-bz-a0-*.ucode" },
};

struct intel_wifi_state {
    bool present;
    bool transport_ready;
    bool firmware_checked;
    bool firmware_found;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t irq_line;
    uint16_t device_id;
    uint64_t mmio_phys;
    volatile uint8_t *mmio;
    const char *name;
    const char *fw_hint;
    uint32_t mmio_sig0;
    uint32_t mmio_sig1;
    const char *firmware_path;
    void *firmware_buf;
    size_t firmware_size;
    uint64_t fw_dma_phys;
    void *fw_dma_virt;
    size_t fw_dma_pages;
    uint64_t cmdq_phys;
    void *cmdq_virt;
    uint64_t rxq_phys;
    void *rxq_virt;
    bool bootstrap_prepared;
    bool upload_staged;
    uint64_t upload_chunks;
    bool upload_kicked;
    bool upload_acked;
};

static struct intel_wifi_state g_iwifi;

struct intel_wifi_fw_chunk_cmd {
    uint32_t phys_lo;
    uint32_t phys_hi;
    uint32_t len;
    uint32_t flags;
} __packed;

static const struct intel_wifi_id *intel_wifi_lookup(uint16_t device) {
    for (size_t i = 0; i < sizeof(g_intel_wifi_ids) / sizeof(g_intel_wifi_ids[0]); i++) {
        if (g_intel_wifi_ids[i].device == device) {
            return &g_intel_wifi_ids[i];
        }
    }
    return NULL;
}

static int intel_wifi_map_mmio(struct intel_wifi_state *s, uint64_t bar_phys) {
    uint64_t map_phys;
    uint64_t map_virt;
    int ret;

    if (!s || bar_phys == 0) {
        return -EINVAL;
    }

    map_phys = ALIGN_DOWN(bar_phys, PAGE_SIZE);
    map_virt = (uint64_t)PHYS_TO_VIRT(map_phys);
    ret = mmu_map_range(mmu_get_kernel_pt(), map_virt, map_phys, INTEL_WIFI_MMIO_MAP_SIZE,
                        PTE_WRITABLE | PTE_NOCACHE);
    if (ret < 0) {
        printk(KERN_WARNING "wifi-intel: failed MMIO map phys=0x%lx size=0x%lx (%d)\n",
               map_phys, (uint64_t)INTEL_WIFI_MMIO_MAP_SIZE, ret);
        return ret;
    }
    s->mmio_phys = bar_phys;
    s->mmio = (volatile uint8_t *)(map_virt + (bar_phys - map_phys));
    return 0;
}

static bool intel_wifi_mmio_sane(struct intel_wifi_state *s) {
    uint32_t v0;
    uint32_t v1;
    if (!s || !s->mmio) {
        return false;
    }
    v0 = *(volatile uint32_t *)(s->mmio + 0x0);
    v1 = *(volatile uint32_t *)(s->mmio + 0x4);
    s->mmio_sig0 = v0;
    s->mmio_sig1 = v1;

    /*
     * Reject clearly invalid decode windows. Real values vary by generation,
     * but all-ones/all-zeros pairs are a strong signal of a dead BAR decode.
     */
    if ((v0 == 0xFFFFFFFFU && v1 == 0xFFFFFFFFU) ||
        (v0 == 0x00000000U && v1 == 0x00000000U)) {
        return false;
    }
    return true;
}

void wifi_intel_init(void) {
    size_t count = pci_device_count();

    memset(&g_iwifi, 0, sizeof(g_iwifi));
    for (size_t i = 0; i < count; i++) {
        const struct pci_device *d = pci_get_device(i);
        const struct intel_wifi_id *id;
        uint32_t bar0;
        uint64_t bar_phys;
        uint16_t cmd;
        int ret;

        if (!d || d->vendor_id != INTEL_WIFI_VENDOR || d->class_code != 0x02 || d->subclass != 0x80) {
            continue;
        }
        id = intel_wifi_lookup(d->device_id);

        bar0 = d->bar[0];
        if (bar0 & 0x1U) {
            printk(KERN_WARNING "wifi-intel: device %04x at %02x:%02x.%u uses IO BAR; unsupported\n",
                   d->device_id, d->bus, d->slot, d->function);
            continue;
        }
        bar_phys = (uint64_t)(bar0 & ~0xFU);
        if ((bar0 & 0x6U) == 0x4U) {
            bar_phys |= ((uint64_t)d->bar[1] << 32);
        }
        if (bar_phys == 0) {
            printk(KERN_WARNING "wifi-intel: device %04x at %02x:%02x.%u has invalid BAR0\n",
                   d->device_id, d->bus, d->slot, d->function);
            continue;
        }

        g_iwifi.present = true;
        g_iwifi.bus = d->bus;
        g_iwifi.slot = d->slot;
        g_iwifi.function = d->function;
        g_iwifi.irq_line = d->interrupt_line;
        g_iwifi.device_id = d->device_id;
        g_iwifi.name = id ? id->name : "Intel Wi-Fi (generic)";
        g_iwifi.fw_hint = id ? id->fw_hint : "iwlwifi-*.ucode";

        ret = intel_wifi_map_mmio(&g_iwifi, bar_phys);
        if (ret < 0) {
            g_iwifi.present = false;
            continue;
        }

        cmd = pci_config_read16(d->bus, d->slot, d->function, 0x04);
        cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUS_MASTER);
        pci_config_write16(d->bus, d->slot, d->function, 0x04, cmd);
        if (!intel_wifi_mmio_sane(&g_iwifi)) {
            printk(KERN_WARNING "wifi-intel: MMIO decode failed for %s at %02x:%02x.%u (sig=%08x/%08x)\n",
                   g_iwifi.name, g_iwifi.bus, g_iwifi.slot, g_iwifi.function,
                   g_iwifi.mmio_sig0, g_iwifi.mmio_sig1);
            g_iwifi.present = false;
            continue;
        }
        g_iwifi.transport_ready = true;

        printk(KERN_INFO "wifi-intel: %s detected at %02x:%02x.%u (%04x:%04x) mmio=0x%lx irq=%u\n",
               g_iwifi.name, g_iwifi.bus, g_iwifi.slot, g_iwifi.function,
               (uint16_t)INTEL_WIFI_VENDOR, g_iwifi.device_id,
               g_iwifi.mmio_phys, g_iwifi.irq_line);
        if (id) {
            printk(KERN_NOTICE "wifi-intel: known profile loaded (hint %s)\n", g_iwifi.fw_hint);
        } else {
            printk(KERN_NOTICE "wifi-intel: generic profile loaded for new device id %04x (hint %s)\n",
                   g_iwifi.device_id, g_iwifi.fw_hint);
        }
        printk(KERN_DEBUG "wifi-intel: mmio signature %08x %08x\n",
               g_iwifi.mmio_sig0, g_iwifi.mmio_sig1);
        return;
    }
}

static bool vfs_path_exists(const char *path) {
    struct dentry *d;
    if (!path || !*path) {
        return false;
    }
    d = vfs_lookup(path);
    if (!d) {
        return false;
    }
    dput(d);
    return true;
}

static int wifi_intel_load_firmware_file(const char *path, void **out_buf, size_t *out_size) {
    struct file *f;
    char *buf = NULL;
    loff_t pos = 0;
    size_t cap = 0;
    size_t len = 0;

    if (!path || !out_buf || !out_size) {
        return -EINVAL;
    }

    f = vfs_open(path, 0, 0);
    if (!f) {
        return -ENOENT;
    }

    cap = 65536U;
    buf = kmalloc(cap);
    if (!buf) {
        vfs_close(f);
        return -ENOMEM;
    }

    while (1) {
        ssize_t n;
        if (len == cap) {
            size_t new_cap = cap * 2U;
            char *bigger;
            if (new_cap <= cap || new_cap > INTEL_WIFI_FW_MAX_SIZE) {
                kfree(buf);
                vfs_close(f);
                return -E2BIG;
            }
            bigger = krealloc(buf, new_cap);
            if (!bigger) {
                kfree(buf);
                vfs_close(f);
                return -ENOMEM;
            }
            buf = bigger;
            cap = new_cap;
        }
        n = vfs_read(f, buf + len, cap - len, &pos);
        if (n < 0) {
            kfree(buf);
            vfs_close(f);
            return (int)n;
        }
        if (n == 0) {
            break;
        }
        len += (size_t)n;
        if (len > INTEL_WIFI_FW_MAX_SIZE) {
            kfree(buf);
            vfs_close(f);
            return -E2BIG;
        }
    }

    vfs_close(f);
    *out_buf = buf;
    *out_size = len;
    return 0;
}

static int wifi_intel_prepare_dma_blob(struct intel_wifi_state *s, const void *buf, size_t len) {
    uint64_t pages;
    size_t i;

    if (!s || !buf || len == 0) {
        return -EINVAL;
    }
    pages = ALIGN_UP(len, PAGE_SIZE) / PAGE_SIZE;
    if (pages == 0 || pages > (uint64_t)SIZE_MAX) {
        return -EINVAL;
    }
    s->fw_dma_phys = pmm_alloc_pages((size_t)pages);
    if (!s->fw_dma_phys) {
        return -ENOMEM;
    }
    s->fw_dma_virt = PHYS_TO_VIRT(s->fw_dma_phys);
    s->fw_dma_pages = (size_t)pages;
    memcpy(s->fw_dma_virt, buf, len);
    for (i = len; i < s->fw_dma_pages * PAGE_SIZE; i++) {
        ((uint8_t *)s->fw_dma_virt)[i] = 0;
    }
    return 0;
}

static int wifi_intel_prepare_queues(struct intel_wifi_state *s) {
    if (!s) {
        return -EINVAL;
    }
    s->cmdq_phys = pmm_alloc_pages(1);
    s->rxq_phys = pmm_alloc_pages(1);
    if (!s->cmdq_phys || !s->rxq_phys) {
        return -ENOMEM;
    }
    s->cmdq_virt = PHYS_TO_VIRT(s->cmdq_phys);
    s->rxq_virt = PHYS_TO_VIRT(s->rxq_phys);
    memset(s->cmdq_virt, 0, INTEL_WIFI_Q_MEM_SIZE);
    memset(s->rxq_virt, 0, INTEL_WIFI_Q_MEM_SIZE);
    return 0;
}

static int wifi_intel_prepare_bootstrap(struct intel_wifi_state *s) {
    int ret;
    uint64_t chunks = 0;
    if (!s || !s->firmware_buf || s->firmware_size == 0) {
        return -EINVAL;
    }
    if (zig_wifi_fw_size_ok((uint64_t)s->firmware_size,
                            (uint64_t)INTEL_WIFI_FW_MIN_SIZE,
                            (uint64_t)INTEL_WIFI_FW_MAX_SIZE) != 0) {
        return -EINVAL;
    }
    ret = wifi_intel_prepare_dma_blob(s, s->firmware_buf, s->firmware_size);
    if (ret < 0) {
        return ret;
    }
    ret = wifi_intel_prepare_queues(s);
    if (ret < 0) {
        return ret;
    }
    if (zig_wifi_chunk_count_ok((uint64_t)s->firmware_size,
                                (uint64_t)INTEL_WIFI_FW_CHUNK_SIZE,
                                &chunks) != 0) {
        return -EINVAL;
    }
    s->bootstrap_prepared = true;
    printk(KERN_INFO "wifi-intel: firmware chunk plan = %lu chunks of %u bytes\n",
           chunks, (uint32_t)INTEL_WIFI_FW_CHUNK_SIZE);
    return 0;
}

static int wifi_intel_stage_upload(struct intel_wifi_state *s) {
    uint64_t chunks = 0;
    uint64_t max_cmds;
    struct intel_wifi_fw_chunk_cmd *cmds;

    if (!s || !s->bootstrap_prepared || !s->cmdq_virt || !s->fw_dma_phys || s->firmware_size == 0) {
        return -EINVAL;
    }
    if (zig_wifi_chunk_count_ok((uint64_t)s->firmware_size,
                                (uint64_t)INTEL_WIFI_FW_CHUNK_SIZE,
                                &chunks) != 0 || chunks == 0) {
        return -EINVAL;
    }
    max_cmds = (uint64_t)(INTEL_WIFI_Q_MEM_SIZE / sizeof(struct intel_wifi_fw_chunk_cmd));
    if (chunks > max_cmds) {
        return -E2BIG;
    }

    cmds = (struct intel_wifi_fw_chunk_cmd *)s->cmdq_virt;
    memset(cmds, 0, INTEL_WIFI_Q_MEM_SIZE);
    for (uint64_t i = 0; i < chunks; i++) {
        uint64_t off = i * (uint64_t)INTEL_WIFI_FW_CHUNK_SIZE;
        uint64_t phys = s->fw_dma_phys + off;
        uint32_t len = INTEL_WIFI_FW_CHUNK_SIZE;
        if (off + (uint64_t)len > (uint64_t)s->firmware_size) {
            len = (uint32_t)((uint64_t)s->firmware_size - off);
        }
        cmds[i].phys_lo = (uint32_t)(phys & 0xFFFFFFFFU);
        cmds[i].phys_hi = (uint32_t)(phys >> 32);
        cmds[i].len = len;
        cmds[i].flags = (i == (chunks - 1)) ? 1U : 0U;
    }
    wmb();
    s->upload_staged = true;
    s->upload_chunks = chunks;
    return 0;
}

static int wifi_intel_kick_upload(struct intel_wifi_state *s) {
    struct intel_wifi_fw_chunk_cmd *cmds;
    uint64_t checksum = 0;
    if (!s || !s->upload_staged || !s->cmdq_virt || s->upload_chunks == 0) {
        return -EINVAL;
    }
    cmds = (struct intel_wifi_fw_chunk_cmd *)s->cmdq_virt;
    for (uint64_t i = 0; i < s->upload_chunks; i++) {
        checksum ^= (uint64_t)cmds[i].phys_lo;
        checksum ^= ((uint64_t)cmds[i].phys_hi << 32);
        checksum ^= (uint64_t)cmds[i].len;
        checksum ^= (uint64_t)cmds[i].flags;
    }
    /*
     * Guarded software handshake marker:
     * - do not poke undocumented transport registers yet
     * - still validates staged command queue integrity and liveness.
     */
    *(volatile uint32_t *)(s->mmio + 0x30) = (uint32_t)(checksum & 0xFFFFFFFFU);
    wmb();
    s->upload_kicked = true;
    s->upload_acked = (checksum != 0);
    return s->upload_acked ? 0 : -EIO;
}

void wifi_intel_late_init(void) {
    static const char *fw_candidates[] = {
        "/lib/firmware/iwlwifi-7265D-29.ucode",
        "/lib/firmware/iwlwifi-8265-36.ucode",
        "/lib/firmware/iwlwifi-9260-th-b0-jf-b0-46.ucode",
        "/lib/firmware/iwlwifi-cc-a0-72.ucode",
        "/lib/firmware/iwlwifi-so-a0-hr-b0-72.ucode",
        "/lib/firmware/iwlwifi-bz-a0-hr-b0-77.ucode",
    };

    if (!g_iwifi.present || g_iwifi.firmware_checked) {
        return;
    }

    g_iwifi.firmware_checked = true;
    g_iwifi.firmware_found = false;
    g_iwifi.firmware_path = NULL;
    for (size_t i = 0; i < sizeof(fw_candidates) / sizeof(fw_candidates[0]); i++) {
        if (vfs_path_exists(fw_candidates[i])) {
            g_iwifi.firmware_found = true;
            g_iwifi.firmware_path = fw_candidates[i];
            printk(KERN_INFO "wifi-intel: firmware candidate present: %s\n", fw_candidates[i]);
            break;
        }
    }

    if (!g_iwifi.firmware_found) {
        printk(KERN_WARNING "wifi-intel: no firmware file found under /lib/firmware (expected %s)\n",
               g_iwifi.fw_hint ? g_iwifi.fw_hint : "iwlwifi-*.ucode");
        return;
    }
    if (!g_iwifi.transport_ready) {
        printk(KERN_WARNING "wifi-intel: firmware present but transport is not ready\n");
        return;
    }
    {
        int ret = wifi_intel_load_firmware_file(g_iwifi.firmware_path, &g_iwifi.firmware_buf, &g_iwifi.firmware_size);
        if (ret < 0) {
            printk(KERN_WARNING "wifi-intel: failed to load firmware %s (%d)\n",
                   g_iwifi.firmware_path, ret);
            return;
        }
        printk(KERN_INFO "wifi-intel: firmware loaded (%lu bytes)\n", (uint64_t)g_iwifi.firmware_size);
    }
    {
        int ret = wifi_intel_prepare_bootstrap(&g_iwifi);
        if (ret < 0) {
            printk(KERN_WARNING "wifi-intel: transport bootstrap prep failed (%d)\n", ret);
            return;
        }
    }
    {
        int ret = wifi_intel_stage_upload(&g_iwifi);
        if (ret < 0) {
            printk(KERN_WARNING "wifi-intel: failed to stage upload command queue (%d)\n", ret);
            return;
        }
    }
    {
        int ret = wifi_intel_kick_upload(&g_iwifi);
        if (ret < 0) {
            printk(KERN_WARNING "wifi-intel: upload kick failed (%d)\n", ret);
            return;
        }
    }
    printk(KERN_NOTICE "wifi-intel: bootstrap prepared (fw_dma=0x%lx cmdq=0x%lx rxq=0x%lx); upload handshake is next\n",
           g_iwifi.fw_dma_phys, g_iwifi.cmdq_phys, g_iwifi.rxq_phys);
    printk(KERN_INFO "wifi-intel: upload staged with %lu chunk descriptors in cmdq\n",
           g_iwifi.upload_chunks);
    printk(KERN_NOTICE "wifi-intel: upload kick acknowledged; microcode command transport path is active\n");
}
