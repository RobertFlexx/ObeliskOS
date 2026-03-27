/*
 * Obelisk OS - Realtek Wi-Fi scaffold
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/mmu.h>
#include <drivers/pci.h>
#include <fs/vfs.h>
#include <mm/pmm.h>
#include <obelisk/zig_wifi.h>
#include <net/wifi_rtl.h>

#define RTLW_VENDOR_ID 0x10EC
#define RTLW_MMIO_MAP_SIZE 0x4000UL
#define RTLW_FW_MAX_SIZE (2U * 1024U * 1024U)
#define RTLW_FW_MIN_SIZE 4096U
#define RTLW_FW_CHUNK_SIZE 4096U
#define RTLW_Q_MEM_SIZE 4096U

#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_MEM 0x2
#define PCI_COMMAND_BUS_MASTER 0x4

struct rtlw_id {
    uint16_t device;
    const char *name;
    const char *fw_hint;
};

static const struct rtlw_id g_rtlw_ids[] = {
    { 0x8176, "Realtek RTL8188CE", "rtlwifi/rtl8188*.bin" },
    { 0x8179, "Realtek RTL8188EE", "rtlwifi/rtl8188*.bin" },
    { 0x818B, "Realtek RTL8192EE", "rtlwifi/rtl8192*.bin" },
    { 0xB822, "Realtek RTL8822BE", "rtw88/rtw8822b*.bin" },
    { 0xB852, "Realtek RTL8852BE", "rtw89/rtw8852b*.bin" },
};

struct rtlw_state {
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

static struct rtlw_state g_rtlw;

struct rtlw_fw_chunk_cmd {
    uint32_t phys_lo;
    uint32_t phys_hi;
    uint32_t len;
    uint32_t flags;
} __packed;

static const struct rtlw_id *rtlw_lookup(uint16_t device) {
    for (size_t i = 0; i < sizeof(g_rtlw_ids) / sizeof(g_rtlw_ids[0]); i++) {
        if (g_rtlw_ids[i].device == device) return &g_rtlw_ids[i];
    }
    return NULL;
}

static bool vfs_exists_rtl(const char *path) {
    struct dentry *d;
    if (!path || !*path) return false;
    d = vfs_lookup(path);
    if (!d) return false;
    dput(d);
    return true;
}

static int load_fw_rtl(const char *path, void **out_buf, size_t *out_size, size_t max_size) {
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

static int rtlw_map_mmio(struct rtlw_state *s, uint64_t bar_phys) {
    uint64_t map_phys = ALIGN_DOWN(bar_phys, PAGE_SIZE);
    uint64_t map_virt = (uint64_t)PHYS_TO_VIRT(map_phys);
    int ret = mmu_map_range(mmu_get_kernel_pt(), map_virt, map_phys, RTLW_MMIO_MAP_SIZE,
                            PTE_WRITABLE | PTE_NOCACHE);
    if (ret < 0) return ret;
    s->mmio_phys = bar_phys;
    s->mmio = (volatile uint8_t *)(map_virt + (bar_phys - map_phys));
    return 0;
}

static int rtlw_prepare_bootstrap(struct rtlw_state *s) {
    uint64_t chunks = 0;
    if (!s || !s->firmware_buf || s->firmware_size == 0) return -EINVAL;
    if (zig_wifi_fw_size_ok((uint64_t)s->firmware_size, RTLW_FW_MIN_SIZE, RTLW_FW_MAX_SIZE) != 0) return -EINVAL;

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
    memset(s->cmdq_virt, 0, RTLW_Q_MEM_SIZE);
    memset(s->rxq_virt, 0, RTLW_Q_MEM_SIZE);

    if (zig_wifi_chunk_count_ok((uint64_t)s->firmware_size, RTLW_FW_CHUNK_SIZE, &chunks) != 0 || chunks == 0) {
        return -EINVAL;
    }
    s->upload_chunks = chunks;
    s->bootstrap_prepared = true;
    return 0;
}

static int rtlw_stage_upload(struct rtlw_state *s) {
    struct rtlw_fw_chunk_cmd *cmds;
    uint64_t max_cmds;
    if (!s || !s->bootstrap_prepared || !s->cmdq_virt) return -EINVAL;
    max_cmds = (uint64_t)(RTLW_Q_MEM_SIZE / sizeof(struct rtlw_fw_chunk_cmd));
    if (s->upload_chunks > max_cmds) return -E2BIG;
    cmds = (struct rtlw_fw_chunk_cmd *)s->cmdq_virt;
    memset(cmds, 0, RTLW_Q_MEM_SIZE);
    for (uint64_t i = 0; i < s->upload_chunks; i++) {
        uint64_t off = i * RTLW_FW_CHUNK_SIZE;
        uint64_t phys = s->fw_dma_phys + off;
        uint32_t len = RTLW_FW_CHUNK_SIZE;
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

void wifi_rtl_init(void) {
    size_t count = pci_device_count();
    memset(&g_rtlw, 0, sizeof(g_rtlw));
    for (size_t i = 0; i < count; i++) {
        const struct pci_device *d = pci_get_device(i);
        const struct rtlw_id *id;
        uint32_t bar0;
        uint64_t bar_phys;
        uint16_t cmd;
        if (!d || d->vendor_id != RTLW_VENDOR_ID || d->class_code != 0x02 || d->subclass != 0x80) continue;
        id = rtlw_lookup(d->device_id);
        bar0 = d->bar[0];
        if (bar0 & 0x1U) continue;
        bar_phys = (uint64_t)(bar0 & ~0xFU);
        if ((bar0 & 0x6U) == 0x4U) bar_phys |= ((uint64_t)d->bar[1] << 32);
        if (!bar_phys) continue;
        g_rtlw.present = true;
        g_rtlw.bus = d->bus; g_rtlw.slot = d->slot; g_rtlw.function = d->function; g_rtlw.irq_line = d->interrupt_line;
        g_rtlw.device_id = d->device_id;
        g_rtlw.name = id ? id->name : "Realtek Wi-Fi (generic)";
        g_rtlw.fw_hint = id ? id->fw_hint : "rtlwifi/rtw*/**/*.bin";
        if (rtlw_map_mmio(&g_rtlw, bar_phys) < 0) {
            g_rtlw.present = false;
            continue;
        }
        cmd = pci_config_read16(d->bus, d->slot, d->function, 0x04);
        cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUS_MASTER);
        pci_config_write16(d->bus, d->slot, d->function, 0x04, cmd);
        g_rtlw.transport_ready = true;
        printk(KERN_INFO "wifi-rtl: %s detected at %02x:%02x.%u (%04x:%04x) mmio=0x%lx irq=%u\n",
               g_rtlw.name, g_rtlw.bus, g_rtlw.slot, g_rtlw.function, (uint16_t)RTLW_VENDOR_ID, g_rtlw.device_id,
               g_rtlw.mmio_phys, g_rtlw.irq_line);
        return;
    }
}

void wifi_rtl_late_init(void) {
    static const char *fw_candidates[] = {
        "/lib/firmware/rtlwifi/rtl8188efw.bin",
        "/lib/firmware/rtlwifi/rtl8192eefw.bin",
        "/lib/firmware/rtw88/rtw8822b_fw.bin",
        "/lib/firmware/rtw89/rtw8852b_fw.bin",
    };
    if (!g_rtlw.present || g_rtlw.firmware_checked) return;
    g_rtlw.firmware_checked = true;
    for (size_t i = 0; i < sizeof(fw_candidates) / sizeof(fw_candidates[0]); i++) {
        if (vfs_exists_rtl(fw_candidates[i])) {
            g_rtlw.firmware_found = true;
            g_rtlw.firmware_path = fw_candidates[i];
            break;
        }
    }
    if (!g_rtlw.firmware_found || !g_rtlw.transport_ready) {
        printk(KERN_WARNING "wifi-rtl: firmware/transport not ready (hint %s)\n", g_rtlw.fw_hint);
        return;
    }
    if (load_fw_rtl(g_rtlw.firmware_path, &g_rtlw.firmware_buf, &g_rtlw.firmware_size, RTLW_FW_MAX_SIZE) < 0) {
        printk(KERN_WARNING "wifi-rtl: firmware load/size check failed\n");
        return;
    }
    if (rtlw_prepare_bootstrap(&g_rtlw) < 0) {
        printk(KERN_WARNING "wifi-rtl: bootstrap prep failed\n");
        return;
    }
    if (rtlw_stage_upload(&g_rtlw) < 0) {
        printk(KERN_WARNING "wifi-rtl: upload staging failed\n");
        return;
    }
    printk(KERN_NOTICE "wifi-rtl: firmware staged (%lu bytes) dma=0x%lx cmdq=0x%lx rxq=0x%lx chunks=%lu\n",
           (uint64_t)g_rtlw.firmware_size, g_rtlw.fw_dma_phys, g_rtlw.cmdq_phys, g_rtlw.rxq_phys, g_rtlw.upload_chunks);
}
