/*
 * Obelisk OS - Broadcom Wi-Fi scaffold
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/mmu.h>
#include <drivers/pci.h>
#include <fs/vfs.h>
#include <mm/pmm.h>
#include <obelisk/zig_wifi.h>
#include <net/wifi_brcm.h>

#define BRCM_VENDOR_ID 0x14E4
#define BRCM_MMIO_MAP_SIZE 0x4000UL
#define BRCM_FW_MAX_SIZE (3U * 1024U * 1024U)
#define BRCM_FW_MIN_SIZE 4096U
#define BRCM_FW_CHUNK_SIZE 4096U
#define BRCM_Q_MEM_SIZE 4096U

#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_MEM 0x2
#define PCI_COMMAND_BUS_MASTER 0x4

struct brcm_id {
    uint16_t device;
    const char *name;
    const char *fw_hint;
};

static const struct brcm_id g_brcm_ids[] = {
    { 0x43A0, "Broadcom BCM4360",  "brcm/brcmfmac4360*.bin" },
    { 0x43B1, "Broadcom BCM4352",  "brcm/brcmfmac4352*.bin" },
    { 0x43C3, "Broadcom BCM43602", "brcm/brcmfmac43602*.bin" },
    { 0x43D5, "Broadcom BCM4366",  "brcm/brcmfmac4366*.bin" },
};

struct brcm_state {
    bool present, transport_ready, firmware_checked, firmware_found;
    uint8_t bus, slot, function, irq_line;
    uint16_t device_id;
    uint64_t mmio_phys;
    volatile uint8_t *mmio;
    const char *name;
    const char *fw_hint;
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
};

static struct brcm_state g_brcm;

struct brcm_fw_chunk_cmd {
    uint32_t phys_lo;
    uint32_t phys_hi;
    uint32_t len;
    uint32_t flags;
} __packed;

static const struct brcm_id *brcm_lookup(uint16_t device) {
    for (size_t i = 0; i < sizeof(g_brcm_ids) / sizeof(g_brcm_ids[0]); i++) {
        if (g_brcm_ids[i].device == device) return &g_brcm_ids[i];
    }
    return NULL;
}

static bool vfs_exists(const char *path) {
    struct dentry *d;
    if (!path || !*path) return false;
    d = vfs_lookup(path);
    if (!d) return false;
    dput(d);
    return true;
}

static int load_fw(const char *path, void **out_buf, size_t *out_size, size_t max_size) {
    struct file *f;
    char *buf;
    size_t cap = 65536U, len = 0;
    loff_t pos = 0;
    if (!path || !out_buf || !out_size) return -EINVAL;
    f = vfs_open(path, 0, 0);
    if (!f) return -ENOENT;
    buf = kmalloc(cap);
    if (!buf) {
        vfs_close(f);
        return -ENOMEM;
    }
    while (1) {
        ssize_t n;
        if (len == cap) {
            size_t ncap = cap * 2U;
            char *b;
            if (ncap <= cap || ncap > max_size) {
                kfree(buf); vfs_close(f); return -E2BIG;
            }
            b = krealloc(buf, ncap);
            if (!b) {
                kfree(buf); vfs_close(f); return -ENOMEM;
            }
            buf = b;
            cap = ncap;
        }
        n = vfs_read(f, buf + len, cap - len, &pos);
        if (n < 0) {
            kfree(buf); vfs_close(f); return (int)n;
        }
        if (n == 0) break;
        len += (size_t)n;
        if (len > max_size) {
            kfree(buf); vfs_close(f); return -E2BIG;
        }
    }
    vfs_close(f);
    *out_buf = buf; *out_size = len;
    return 0;
}

static int brcm_map_mmio(struct brcm_state *s, uint64_t bar_phys) {
    uint64_t map_phys = ALIGN_DOWN(bar_phys, PAGE_SIZE);
    uint64_t map_virt = (uint64_t)PHYS_TO_VIRT(map_phys);
    int ret = mmu_map_range(mmu_get_kernel_pt(), map_virt, map_phys, BRCM_MMIO_MAP_SIZE,
                            PTE_WRITABLE | PTE_NOCACHE);
    if (ret < 0) return ret;
    s->mmio_phys = bar_phys;
    s->mmio = (volatile uint8_t *)(map_virt + (bar_phys - map_phys));
    return 0;
}

static int brcm_prepare_bootstrap(struct brcm_state *s) {
    uint64_t chunks = 0;
    if (!s || !s->firmware_buf || s->firmware_size == 0) return -EINVAL;
    if (zig_wifi_fw_size_ok((uint64_t)s->firmware_size, BRCM_FW_MIN_SIZE, BRCM_FW_MAX_SIZE) != 0) return -EINVAL;

    s->fw_dma_pages = ALIGN_UP(s->firmware_size, PAGE_SIZE) / PAGE_SIZE;
    s->fw_dma_phys = pmm_alloc_pages(s->fw_dma_pages);
    if (!s->fw_dma_phys) return -ENOMEM;
    s->fw_dma_virt = PHYS_TO_VIRT(s->fw_dma_phys);
    memcpy(s->fw_dma_virt, s->firmware_buf, s->firmware_size);

    s->cmdq_phys = pmm_alloc_pages(1);
    s->rxq_phys = pmm_alloc_pages(1);
    if (!s->cmdq_phys || !s->rxq_phys) return -ENOMEM;
    s->cmdq_virt = PHYS_TO_VIRT(s->cmdq_phys);
    s->rxq_virt = PHYS_TO_VIRT(s->rxq_phys);
    memset(s->cmdq_virt, 0, BRCM_Q_MEM_SIZE);
    memset(s->rxq_virt, 0, BRCM_Q_MEM_SIZE);

    if (zig_wifi_chunk_count_ok((uint64_t)s->firmware_size, BRCM_FW_CHUNK_SIZE, &chunks) != 0 || chunks == 0) {
        return -EINVAL;
    }
    s->upload_chunks = chunks;
    s->bootstrap_prepared = true;
    return 0;
}

static int brcm_stage_upload(struct brcm_state *s) {
    struct brcm_fw_chunk_cmd *cmds;
    uint64_t max_cmds;
    if (!s || !s->bootstrap_prepared || !s->cmdq_virt) return -EINVAL;
    max_cmds = (uint64_t)(BRCM_Q_MEM_SIZE / sizeof(struct brcm_fw_chunk_cmd));
    if (s->upload_chunks > max_cmds) return -E2BIG;
    cmds = (struct brcm_fw_chunk_cmd *)s->cmdq_virt;
    memset(cmds, 0, BRCM_Q_MEM_SIZE);
    for (uint64_t i = 0; i < s->upload_chunks; i++) {
        uint64_t off = i * BRCM_FW_CHUNK_SIZE;
        uint64_t phys = s->fw_dma_phys + off;
        uint32_t len = BRCM_FW_CHUNK_SIZE;
        if (off + (uint64_t)len > (uint64_t)s->firmware_size) {
            len = (uint32_t)((uint64_t)s->firmware_size - off);
        }
        cmds[i].phys_lo = (uint32_t)(phys & 0xFFFFFFFFU);
        cmds[i].phys_hi = (uint32_t)(phys >> 32);
        cmds[i].len = len;
        cmds[i].flags = (i == (s->upload_chunks - 1)) ? 1U : 0U;
    }
    wmb();
    s->upload_staged = true;
    return 0;
}

void wifi_brcm_init(void) {
    size_t count = pci_device_count();
    memset(&g_brcm, 0, sizeof(g_brcm));
    for (size_t i = 0; i < count; i++) {
        const struct pci_device *d = pci_get_device(i);
        const struct brcm_id *id;
        uint32_t bar0;
        uint64_t bar_phys;
        uint16_t cmd;
        if (!d || d->vendor_id != BRCM_VENDOR_ID || d->class_code != 0x02 || d->subclass != 0x80) continue;
        id = brcm_lookup(d->device_id);
        bar0 = d->bar[0];
        if (bar0 & 0x1U) continue;
        bar_phys = (uint64_t)(bar0 & ~0xFU);
        if ((bar0 & 0x6U) == 0x4U) bar_phys |= ((uint64_t)d->bar[1] << 32);
        if (!bar_phys) continue;
        g_brcm.present = true;
        g_brcm.bus = d->bus; g_brcm.slot = d->slot; g_brcm.function = d->function; g_brcm.irq_line = d->interrupt_line;
        g_brcm.device_id = d->device_id;
        g_brcm.name = id ? id->name : "Broadcom Wi-Fi (generic)";
        g_brcm.fw_hint = id ? id->fw_hint : "brcm/brcmfmac*.bin";
        if (brcm_map_mmio(&g_brcm, bar_phys) < 0) {
            g_brcm.present = false;
            continue;
        }
        cmd = pci_config_read16(d->bus, d->slot, d->function, 0x04);
        cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUS_MASTER);
        pci_config_write16(d->bus, d->slot, d->function, 0x04, cmd);
        g_brcm.transport_ready = true;
        printk(KERN_INFO "wifi-brcm: %s detected at %02x:%02x.%u (%04x:%04x) mmio=0x%lx irq=%u\n",
               g_brcm.name, g_brcm.bus, g_brcm.slot, g_brcm.function, (uint16_t)BRCM_VENDOR_ID, g_brcm.device_id,
               g_brcm.mmio_phys, g_brcm.irq_line);
        return;
    }
}

void wifi_brcm_late_init(void) {
    static const char *fw_candidates[] = {
        "/lib/firmware/brcm/brcmfmac4360-pcie.bin",
        "/lib/firmware/brcm/brcmfmac4352-pcie.bin",
        "/lib/firmware/brcm/brcmfmac43602-pcie.bin",
    };
    if (!g_brcm.present || g_brcm.firmware_checked) return;
    g_brcm.firmware_checked = true;
    for (size_t i = 0; i < sizeof(fw_candidates) / sizeof(fw_candidates[0]); i++) {
        if (vfs_exists(fw_candidates[i])) {
            g_brcm.firmware_found = true;
            g_brcm.firmware_path = fw_candidates[i];
            break;
        }
    }
    if (!g_brcm.firmware_found || !g_brcm.transport_ready) {
        printk(KERN_WARNING "wifi-brcm: firmware/transport not ready (hint %s)\n", g_brcm.fw_hint);
        return;
    }
    if (load_fw(g_brcm.firmware_path, &g_brcm.firmware_buf, &g_brcm.firmware_size, BRCM_FW_MAX_SIZE) < 0) {
        printk(KERN_WARNING "wifi-brcm: firmware load/size check failed\n");
        return;
    }
    if (brcm_prepare_bootstrap(&g_brcm) < 0) {
        printk(KERN_WARNING "wifi-brcm: bootstrap prep failed\n");
        return;
    }
    if (brcm_stage_upload(&g_brcm) < 0) {
        printk(KERN_WARNING "wifi-brcm: upload staging failed\n");
        return;
    }
    printk(KERN_NOTICE "wifi-brcm: firmware staged (%lu bytes) dma=0x%lx cmdq=0x%lx rxq=0x%lx chunks=%lu\n",
           (uint64_t)g_brcm.firmware_size, g_brcm.fw_dma_phys, g_brcm.cmdq_phys, g_brcm.rxq_phys, g_brcm.upload_chunks);
}
