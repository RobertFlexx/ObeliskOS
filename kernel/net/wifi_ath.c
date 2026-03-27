/*
 * Obelisk OS - Atheros/QCA Wi-Fi scaffold
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/mmu.h>
#include <drivers/pci.h>
#include <fs/vfs.h>
#include <mm/pmm.h>
#include <obelisk/zig_wifi.h>
#include <net/wifi_ath.h>

#define ATH_VENDOR_ID 0x168C
#define ATH_MMIO_MAP_SIZE 0x4000UL
#define ATH_FW_MAX_SIZE (2U * 1024U * 1024U)
#define ATH_FW_MIN_SIZE 4096U
#define ATH_FW_CHUNK_SIZE 4096U
#define ATH_Q_MEM_SIZE 4096U

#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_MEM 0x2
#define PCI_COMMAND_BUS_MASTER 0x4

struct ath_id {
    uint16_t device;
    const char *name;
    const char *fw_hint;
};

static const struct ath_id g_ath_ids[] = {
    { 0x0030, "Atheros AR93xx", "ath9k/ar93xx*.bin" },
    { 0x0032, "Atheros QCA95xx", "ath9k/qca95xx*.bin" },
    { 0x0042, "Atheros QCA9377", "ath10k/QCA9377/*.bin" },
    { 0x0046, "Atheros QCA6174", "ath10k/QCA6174/*.bin" },
};

struct ath_state {
    bool present;
    bool transport_ready;
    bool firmware_checked;
    bool firmware_found;
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

static struct ath_state g_ath;

struct ath_fw_chunk_cmd {
    uint32_t phys_lo;
    uint32_t phys_hi;
    uint32_t len;
    uint32_t flags;
} __packed;

static const struct ath_id *ath_lookup(uint16_t device) {
    for (size_t i = 0; i < sizeof(g_ath_ids) / sizeof(g_ath_ids[0]); i++) {
        if (g_ath_ids[i].device == device) return &g_ath_ids[i];
    }
    return NULL;
}

static bool vfs_path_exists_local(const char *path) {
    struct dentry *d;
    if (!path || !*path) return false;
    d = vfs_lookup(path);
    if (!d) return false;
    dput(d);
    return true;
}

static int load_fw_file(const char *path, void **out_buf, size_t *out_size, size_t max_size) {
    struct file *f;
    char *buf;
    size_t cap = 65536U;
    size_t len = 0;
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
                kfree(buf);
                vfs_close(f);
                return -E2BIG;
            }
            b = krealloc(buf, ncap);
            if (!b) {
                kfree(buf);
                vfs_close(f);
                return -ENOMEM;
            }
            buf = b;
            cap = ncap;
        }
        n = vfs_read(f, buf + len, cap - len, &pos);
        if (n < 0) {
            kfree(buf);
            vfs_close(f);
            return (int)n;
        }
        if (n == 0) break;
        len += (size_t)n;
        if (len > max_size) {
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

static int ath_map_mmio(struct ath_state *s, uint64_t bar_phys) {
    uint64_t map_phys = ALIGN_DOWN(bar_phys, PAGE_SIZE);
    uint64_t map_virt = (uint64_t)PHYS_TO_VIRT(map_phys);
    int ret;
    ret = mmu_map_range(mmu_get_kernel_pt(), map_virt, map_phys, ATH_MMIO_MAP_SIZE,
                        PTE_WRITABLE | PTE_NOCACHE);
    if (ret < 0) return ret;
    s->mmio_phys = bar_phys;
    s->mmio = (volatile uint8_t *)(map_virt + (bar_phys - map_phys));
    return 0;
}

static int ath_prepare_bootstrap(struct ath_state *s) {
    uint64_t chunks = 0;
    if (!s || !s->firmware_buf || s->firmware_size == 0) {
        return -EINVAL;
    }
    if (zig_wifi_fw_size_ok((uint64_t)s->firmware_size, ATH_FW_MIN_SIZE, ATH_FW_MAX_SIZE) != 0) {
        return -EINVAL;
    }
    s->fw_dma_pages = ALIGN_UP(s->firmware_size, PAGE_SIZE) / PAGE_SIZE;
    s->fw_dma_phys = pmm_alloc_pages(s->fw_dma_pages);
    if (!s->fw_dma_phys) {
        return -ENOMEM;
    }
    s->fw_dma_virt = PHYS_TO_VIRT(s->fw_dma_phys);
    memcpy(s->fw_dma_virt, s->firmware_buf, s->firmware_size);

    s->cmdq_phys = pmm_alloc_pages(1);
    s->rxq_phys = pmm_alloc_pages(1);
    if (!s->cmdq_phys || !s->rxq_phys) {
        return -ENOMEM;
    }
    s->cmdq_virt = PHYS_TO_VIRT(s->cmdq_phys);
    s->rxq_virt = PHYS_TO_VIRT(s->rxq_phys);
    memset(s->cmdq_virt, 0, ATH_Q_MEM_SIZE);
    memset(s->rxq_virt, 0, ATH_Q_MEM_SIZE);

    if (zig_wifi_chunk_count_ok((uint64_t)s->firmware_size, ATH_FW_CHUNK_SIZE, &chunks) != 0 || chunks == 0) {
        return -EINVAL;
    }
    s->upload_chunks = chunks;
    s->bootstrap_prepared = true;
    return 0;
}

static int ath_stage_upload(struct ath_state *s) {
    struct ath_fw_chunk_cmd *cmds;
    uint64_t max_cmds;
    if (!s || !s->bootstrap_prepared || !s->cmdq_virt) {
        return -EINVAL;
    }
    max_cmds = (uint64_t)(ATH_Q_MEM_SIZE / sizeof(struct ath_fw_chunk_cmd));
    if (s->upload_chunks > max_cmds) {
        return -E2BIG;
    }
    cmds = (struct ath_fw_chunk_cmd *)s->cmdq_virt;
    memset(cmds, 0, ATH_Q_MEM_SIZE);
    for (uint64_t i = 0; i < s->upload_chunks; i++) {
        uint64_t off = i * ATH_FW_CHUNK_SIZE;
        uint64_t phys = s->fw_dma_phys + off;
        uint32_t len = ATH_FW_CHUNK_SIZE;
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

void wifi_ath_init(void) {
    size_t count = pci_device_count();
    memset(&g_ath, 0, sizeof(g_ath));
    for (size_t i = 0; i < count; i++) {
        const struct pci_device *d = pci_get_device(i);
        const struct ath_id *id;
        uint32_t bar0;
        uint64_t bar_phys;
        uint16_t cmd;
        if (!d || d->vendor_id != ATH_VENDOR_ID || d->class_code != 0x02 || d->subclass != 0x80) continue;
        id = ath_lookup(d->device_id);
        bar0 = d->bar[0];
        if (bar0 & 0x1U) continue;
        bar_phys = (uint64_t)(bar0 & ~0xFU);
        if ((bar0 & 0x6U) == 0x4U) bar_phys |= ((uint64_t)d->bar[1] << 32);
        if (!bar_phys) continue;

        g_ath.present = true;
        g_ath.bus = d->bus; g_ath.slot = d->slot; g_ath.function = d->function; g_ath.irq_line = d->interrupt_line;
        g_ath.device_id = d->device_id;
        g_ath.name = id ? id->name : "Atheros Wi-Fi (generic)";
        g_ath.fw_hint = id ? id->fw_hint : "ath*/**/*.bin";
        if (ath_map_mmio(&g_ath, bar_phys) < 0) {
            g_ath.present = false;
            continue;
        }
        cmd = pci_config_read16(d->bus, d->slot, d->function, 0x04);
        cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUS_MASTER);
        pci_config_write16(d->bus, d->slot, d->function, 0x04, cmd);
        g_ath.transport_ready = true;
        printk(KERN_INFO "wifi-ath: %s detected at %02x:%02x.%u (%04x:%04x) mmio=0x%lx irq=%u\n",
               g_ath.name, g_ath.bus, g_ath.slot, g_ath.function, (uint16_t)ATH_VENDOR_ID, g_ath.device_id,
               g_ath.mmio_phys, g_ath.irq_line);
        return;
    }
}

void wifi_ath_late_init(void) {
    static const char *fw_candidates[] = {
        "/lib/firmware/ath9k/ar9300_fw.bin",
        "/lib/firmware/ath10k/QCA9377/hw1.0/firmware-6.bin",
        "/lib/firmware/ath10k/QCA6174/hw3.0/firmware-6.bin",
    };
    if (!g_ath.present || g_ath.firmware_checked) return;
    g_ath.firmware_checked = true;
    for (size_t i = 0; i < sizeof(fw_candidates) / sizeof(fw_candidates[0]); i++) {
        if (vfs_path_exists_local(fw_candidates[i])) {
            g_ath.firmware_found = true;
            g_ath.firmware_path = fw_candidates[i];
            break;
        }
    }
    if (!g_ath.firmware_found || !g_ath.transport_ready) {
        printk(KERN_WARNING "wifi-ath: firmware/transport not ready (hint %s)\n", g_ath.fw_hint);
        return;
    }
    if (load_fw_file(g_ath.firmware_path, &g_ath.firmware_buf, &g_ath.firmware_size, ATH_FW_MAX_SIZE) < 0) {
        printk(KERN_WARNING "wifi-ath: firmware load/size check failed\n");
        return;
    }
    if (ath_prepare_bootstrap(&g_ath) < 0) {
        printk(KERN_WARNING "wifi-ath: bootstrap prep failed\n");
        return;
    }
    if (ath_stage_upload(&g_ath) < 0) {
        printk(KERN_WARNING "wifi-ath: upload staging failed\n");
        return;
    }
    printk(KERN_NOTICE "wifi-ath: firmware staged (%lu bytes) dma=0x%lx cmdq=0x%lx rxq=0x%lx chunks=%lu\n",
           (uint64_t)g_ath.firmware_size, g_ath.fw_dma_phys, g_ath.cmdq_phys, g_ath.rxq_phys, g_ath.upload_chunks);
}
