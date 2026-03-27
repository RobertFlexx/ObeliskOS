/*
 * Obelisk OS - Realtek RTL8169 family (step 2 datapath)
 * From Axioms, Order.
 *
 * Scope in this step:
 * - PCI probe and MMIO map
 * - Minimal RX/TX descriptor rings
 * - Polled datapath wired into net core
 * I also really like milkshakes. (fun fact) */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/mmu.h>
#include <drivers/pci.h>
#include <mm/pmm.h>
#include <net/net.h>
#include <net/r8169.h>
#include <obelisk/zig_net.h>

#define RTL_VENDOR_ID 0x10EC

#define RTL_REG_IDR0        0x00
#define RTL_REG_TNPDS_LO    0x20
#define RTL_REG_TNPDS_HI    0x24
#define RTL_REG_TPPOLL      0x38
#define RTL_REG_CHIPCMD     0x37
#define RTL_REG_INTRMASK    0x3C
#define RTL_REG_INTRSTATUS  0x3E
#define RTL_REG_RCR         0x44
#define RTL_REG_TCR         0x40
#define RTL_REG_9346CR      0x50
#define RTL_REG_RXMAX       0xDA
#define RTL_REG_RDSAR_LO    0xE4
#define RTL_REG_RDSAR_HI    0xE8

#define RTL_CHIPCMD_RESET   BIT(4)
#define RTL_CHIPCMD_RE      BIT(3)
#define RTL_CHIPCMD_TE      BIT(2)
#define RTL_TPPOLL_NPQ      BIT(6)

#define RTL_9346_UNLOCK     0xC0
#define RTL_9346_LOCK       0x00

#define RTL_DESC_OWN        BIT(31)
#define RTL_DESC_EOR        BIT(30)
#define RTL_DESC_FS         BIT(29)
#define RTL_DESC_LS         BIT(28)
#define RTL_DESC_LEN_MASK   0x3FFFU

#define RTL_RX_RING_SIZE    64
#define RTL_TX_RING_SIZE    64
#define RTL_PKT_BUF_SIZE    2048

#define RTL_RCR_DEF         0x0000E70FU
#define RTL_TCR_DEF         0x03000700U

#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_MEM 0x2
#define PCI_COMMAND_BUS_MASTER 0x4

#define RTL_MMIO_MAP_SIZE 0x1000UL

struct rtl_desc {
    uint32_t opts1;
    uint32_t opts2;
    uint64_t addr;
} __packed;

struct rtl_state {
    bool present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t device_id;
    uint64_t mmio_phys;
    volatile uint8_t *mmio;
    uint64_t rx_ring_phys;
    uint64_t tx_ring_phys;
    struct rtl_desc *rx_ring;
    struct rtl_desc *tx_ring;
    uint64_t rx_buf_phys[RTL_RX_RING_SIZE];
    uint8_t *rx_buf_virt[RTL_RX_RING_SIZE];
    uint64_t tx_buf_phys[RTL_TX_RING_SIZE];
    uint8_t *tx_buf_virt[RTL_TX_RING_SIZE];
    uint32_t rx_tail;
    uint32_t tx_tail;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_drops;
    uint64_t tx_timeouts;
    uint8_t mac[6];
};

static struct rtl_state g_rtl;
static int rtl_tx(void *ctx, const void *frame, size_t len);
static int rtl_poll(void *ctx);
static const struct net_device_ops g_rtl_ops = {
    .tx = rtl_tx,
    .poll = rtl_poll,
};

static bool rtl_mac_is_valid(const uint8_t mac[6]) {
    return zig_net_mac_is_valid(mac, 6) == 1;
}

static inline uint8_t rtl_read8(struct rtl_state *s, uint32_t reg) {
    return *(volatile uint8_t *)(s->mmio + reg);
}

static inline uint16_t rtl_read16(struct rtl_state *s, uint32_t reg) {
    return *(volatile uint16_t *)(s->mmio + reg);
}

static inline uint32_t rtl_read32(struct rtl_state *s, uint32_t reg) {
    return *(volatile uint32_t *)(s->mmio + reg);
}

static inline void rtl_write8(struct rtl_state *s, uint32_t reg, uint8_t v) {
    *(volatile uint8_t *)(s->mmio + reg) = v;
}

static inline void rtl_write16(struct rtl_state *s, uint32_t reg, uint16_t v) {
    *(volatile uint16_t *)(s->mmio + reg) = v;
}

static inline void rtl_write32(struct rtl_state *s, uint32_t reg, uint32_t v) {
    *(volatile uint32_t *)(s->mmio + reg) = v;
}

static bool rtl_ring_alloc(struct rtl_state *s) {
    uint64_t ring_bytes = 0;
    if (!s) {
        return false;
    }
    if (zig_net_ring_bytes_ok(RTL_RX_RING_SIZE, sizeof(struct rtl_desc), 16, PAGE_SIZE, &ring_bytes) != 0 ||
        zig_net_ring_bytes_ok(RTL_TX_RING_SIZE, sizeof(struct rtl_desc), 16, PAGE_SIZE, &ring_bytes) != 0) {
        return false;
    }

    s->rx_ring_phys = pmm_alloc_pages(1);
    s->tx_ring_phys = pmm_alloc_pages(1);
    if (!s->rx_ring_phys || !s->tx_ring_phys) {
        return false;
    }
    s->rx_ring = (struct rtl_desc *)PHYS_TO_VIRT(s->rx_ring_phys);
    s->tx_ring = (struct rtl_desc *)PHYS_TO_VIRT(s->tx_ring_phys);
    memset(s->rx_ring, 0, PAGE_SIZE);
    memset(s->tx_ring, 0, PAGE_SIZE);

    for (uint32_t i = 0; i < RTL_RX_RING_SIZE; i++) {
        uint64_t p = pmm_alloc_page();
        uint32_t opts = RTL_DESC_OWN | RTL_PKT_BUF_SIZE;
        if (!p) {
            return false;
        }
        s->rx_buf_phys[i] = p;
        s->rx_buf_virt[i] = (uint8_t *)PHYS_TO_VIRT(p);
        memset(s->rx_buf_virt[i], 0, PAGE_SIZE);
        if (i == (RTL_RX_RING_SIZE - 1U)) {
            opts |= RTL_DESC_EOR;
        }
        s->rx_ring[i].addr = p;
        s->rx_ring[i].opts2 = 0;
        s->rx_ring[i].opts1 = opts;
    }
    for (uint32_t i = 0; i < RTL_TX_RING_SIZE; i++) {
        uint64_t p = pmm_alloc_page();
        uint32_t opts = 0;
        if (!p) {
            return false;
        }
        s->tx_buf_phys[i] = p;
        s->tx_buf_virt[i] = (uint8_t *)PHYS_TO_VIRT(p);
        memset(s->tx_buf_virt[i], 0, PAGE_SIZE);
        if (i == (RTL_TX_RING_SIZE - 1U)) {
            opts |= RTL_DESC_EOR;
        }
        s->tx_ring[i].addr = p;
        s->tx_ring[i].opts2 = 0;
        s->tx_ring[i].opts1 = opts;
    }
    s->rx_tail = 0;
    s->tx_tail = 0;
    return true;
}

static bool rtl_device_id_supported(uint16_t id) {
    switch (id) {
        case 0x8161:
        case 0x8167:
        case 0x8168:
        case 0x8169:
            return true;
        default:
            return false;
    }
}

static bool rtl_pci_match(const struct pci_device *d) {
    if (!d) return false;
    if (d->vendor_id != RTL_VENDOR_ID) return false;
    if (d->class_code != 0x02) return false;
    return rtl_device_id_supported(d->device_id);
}

static int rtl_map_mmio(struct rtl_state *s, uint64_t bar_phys) {
    uint64_t map_phys;
    uint64_t map_virt;
    int ret;

    if (!s || bar_phys == 0) {
        return -EINVAL;
    }
    map_phys = ALIGN_DOWN(bar_phys, PAGE_SIZE);
    map_virt = (uint64_t)PHYS_TO_VIRT(map_phys);
    ret = mmu_map_range(mmu_get_kernel_pt(), map_virt, map_phys, RTL_MMIO_MAP_SIZE,
                        PTE_WRITABLE | PTE_NOCACHE);
    if (ret < 0) {
        printk(KERN_WARNING "r8169: failed to map MMIO phys=0x%lx size=0x%lx (%d)\n",
               map_phys, (uint64_t)RTL_MMIO_MAP_SIZE, ret);
        return ret;
    }
    s->mmio_phys = bar_phys;
    s->mmio = (volatile uint8_t *)(map_virt + (bar_phys - map_phys));
    return 0;
}

static bool rtl_wait_reset_done(struct rtl_state *s) {
    for (int i = 0; i < 200000; i++) {
        if ((rtl_read8(s, RTL_REG_CHIPCMD) & RTL_CHIPCMD_RESET) == 0) {
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static void rtl_hw_start(struct rtl_state *s) {
    rtl_write8(s, RTL_REG_9346CR, RTL_9346_UNLOCK);
    rtl_write16(s, RTL_REG_INTRMASK, 0);
    rtl_write16(s, RTL_REG_INTRSTATUS, 0xFFFFU);
    rtl_write32(s, RTL_REG_TNPDS_LO, (uint32_t)(s->tx_ring_phys & 0xFFFFFFFFU));
    rtl_write32(s, RTL_REG_TNPDS_HI, (uint32_t)(s->tx_ring_phys >> 32));
    rtl_write32(s, RTL_REG_RDSAR_LO, (uint32_t)(s->rx_ring_phys & 0xFFFFFFFFU));
    rtl_write32(s, RTL_REG_RDSAR_HI, (uint32_t)(s->rx_ring_phys >> 32));
    rtl_write16(s, RTL_REG_RXMAX, RTL_PKT_BUF_SIZE);
    rtl_write32(s, RTL_REG_RCR, RTL_RCR_DEF);
    rtl_write32(s, RTL_REG_TCR, RTL_TCR_DEF);
    rtl_write8(s, RTL_REG_CHIPCMD, RTL_CHIPCMD_RE | RTL_CHIPCMD_TE);
    rtl_write8(s, RTL_REG_9346CR, RTL_9346_LOCK);
}

static int rtl_tx(void *ctx, const void *frame, size_t len) {
    struct rtl_state *s = (struct rtl_state *)ctx;
    struct rtl_desc *d;
    uint32_t idx;
    uint32_t opts;

    if (!s || !s->present || !frame || len == 0) {
        return -EINVAL;
    }
    if (zig_net_frame_len_ok((uint64_t)len, 1, RTL_PKT_BUF_SIZE) != 0) {
        return -EMSGSIZE;
    }

    idx = s->tx_tail;
    d = &s->tx_ring[idx];
    if (d->opts1 & RTL_DESC_OWN) {
        return -EAGAIN;
    }

    memcpy(s->tx_buf_virt[idx], frame, len);
    opts = (uint32_t)(len & RTL_DESC_LEN_MASK) | RTL_DESC_FS | RTL_DESC_LS | RTL_DESC_OWN;
    if (idx == (RTL_TX_RING_SIZE - 1U)) {
        opts |= RTL_DESC_EOR;
    }
    d->opts2 = 0;
    wmb();
    d->opts1 = opts;
    rtl_write8(s, RTL_REG_TPPOLL, RTL_TPPOLL_NPQ);
    s->tx_tail = (idx + 1U) % RTL_TX_RING_SIZE;

    for (int spin = 0; spin < 200000; spin++) {
        if ((d->opts1 & RTL_DESC_OWN) == 0) {
            s->tx_packets++;
            s->tx_bytes += len;
            return 0;
        }
        __asm__ volatile("pause");
    }
    s->tx_timeouts++;
    return -EIO;
}

static int rtl_poll(void *ctx) {
    struct rtl_state *s = (struct rtl_state *)ctx;
    uint32_t budget = RTL_RX_RING_SIZE;
    uint16_t isr;

    if (!s || !s->present) {
        return -ENODEV;
    }

    isr = rtl_read16(s, RTL_REG_INTRSTATUS);
    if (isr) {
        rtl_write16(s, RTL_REG_INTRSTATUS, isr);
    }

    while (budget-- > 0) {
        struct rtl_desc *d = &s->rx_ring[s->rx_tail];
        uint32_t opts = d->opts1;
        size_t pkt_len = (size_t)(opts & RTL_DESC_LEN_MASK);

        if (opts & RTL_DESC_OWN) {
            break;
        }
        if ((opts & (RTL_DESC_FS | RTL_DESC_LS)) == (RTL_DESC_FS | RTL_DESC_LS) &&
            pkt_len > 4 && pkt_len <= RTL_PKT_BUF_SIZE) {
            /* Trim trailing FCS. */
            net_rx_frame(s->rx_buf_virt[s->rx_tail], pkt_len - 4);
            s->rx_packets++;
            s->rx_bytes += (pkt_len - 4);
        } else {
            s->rx_drops++;
        }

        opts = RTL_DESC_OWN | RTL_PKT_BUF_SIZE;
        if (s->rx_tail == (RTL_RX_RING_SIZE - 1U)) {
            opts |= RTL_DESC_EOR;
        }
        d->opts2 = 0;
        wmb();
        d->opts1 = opts;
        s->rx_tail = (s->rx_tail + 1U) % RTL_RX_RING_SIZE;
    }
    return 0;
}

void r8169_init(void) {
    size_t count = pci_device_count();
    bool matched = false;

    if (net_is_ready()) {
        return;
    }

    memset(&g_rtl, 0, sizeof(g_rtl));
    for (size_t i = 0; i < count; i++) {
        const struct pci_device *d = pci_get_device(i);
        uint32_t bar0;
        uint64_t bar_phys;
        uint16_t cmd;
        int ret;

        if (!rtl_pci_match(d)) {
            continue;
        }
        matched = true;

        bar0 = d->bar[0];
        if (bar0 & 0x1U) {
            printk(KERN_WARNING "r8169: skip %04x at %02x:%02x.%u: IO BAR mode unsupported in step1\n",
                   d->device_id, d->bus, d->slot, d->function);
            continue;
        }
        bar_phys = (uint64_t)(bar0 & ~0xFU);
        if ((bar0 & 0x6U) == 0x4U) {
            bar_phys |= ((uint64_t)d->bar[1] << 32);
        }
        if (bar_phys == 0) {
            printk(KERN_WARNING "r8169: skip %04x at %02x:%02x.%u: invalid BAR0\n",
                   d->device_id, d->bus, d->slot, d->function);
            continue;
        }

        g_rtl.present = true;
        g_rtl.bus = d->bus;
        g_rtl.slot = d->slot;
        g_rtl.function = d->function;
        g_rtl.device_id = d->device_id;

        ret = rtl_map_mmio(&g_rtl, bar_phys);
        if (ret < 0) {
            g_rtl.present = false;
            continue;
        }

        cmd = pci_config_read16(d->bus, d->slot, d->function, 0x04);
        cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUS_MASTER);
        pci_config_write16(d->bus, d->slot, d->function, 0x04, cmd);

        rtl_write8(&g_rtl, RTL_REG_CHIPCMD, RTL_CHIPCMD_RESET);
        if (!rtl_wait_reset_done(&g_rtl)) {
            printk(KERN_WARNING "r8169: reset timeout on %02x:%02x.%u\n",
                   d->bus, d->slot, d->function);
            g_rtl.present = false;
            continue;
        }

        if (!rtl_ring_alloc(&g_rtl)) {
            printk(KERN_WARNING "r8169: ring/buffer allocation failed on %02x:%02x.%u\n",
                   d->bus, d->slot, d->function);
            g_rtl.present = false;
            continue;
        }
        rtl_hw_start(&g_rtl);

        for (int b = 0; b < 6; b++) {
            g_rtl.mac[b] = rtl_read8(&g_rtl, RTL_REG_IDR0 + (uint32_t)b);
        }
        if (!rtl_mac_is_valid(g_rtl.mac)) {
            g_rtl.mac[0] = 0x02;
            g_rtl.mac[1] = 0x52;
            g_rtl.mac[2] = 0x54;
            g_rtl.mac[3] = 0x31;
            g_rtl.mac[4] = 0x69;
            g_rtl.mac[5] = (uint8_t)g_rtl.device_id;
            printk(KERN_WARNING "r8169: invalid hardware MAC; using local fallback MAC\n");
        }

        printk(KERN_INFO "r8169: detected %04x at %02x:%02x.%u mmio=0x%lx mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
               g_rtl.device_id, g_rtl.bus, g_rtl.slot, g_rtl.function, g_rtl.mmio_phys,
               g_rtl.mac[0], g_rtl.mac[1], g_rtl.mac[2],
               g_rtl.mac[3], g_rtl.mac[4], g_rtl.mac[5]);
        ret = net_register_device("r8169", g_rtl.mac, &g_rtl_ops, &g_rtl);
        if (ret < 0) {
            printk(KERN_WARNING "r8169: net registration failed: %d\n", ret);
            g_rtl.present = false;
            continue;
        }
        printk(KERN_DEBUG "r8169: chipcmd=0x%x rcr=0x%x tcr=0x%x\n",
               rtl_read8(&g_rtl, RTL_REG_CHIPCMD),
               rtl_read32(&g_rtl, RTL_REG_RCR),
               rtl_read32(&g_rtl, RTL_REG_TCR));
        return;
    }

    if (!matched) {
        printk(KERN_INFO "r8169: no supported Realtek RTL8169/8111 family NIC found\n");
    }
}
