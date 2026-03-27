/*
 * Obelisk OS - Intel E1000 Driver (minimal)
 * From Axioms, Order.
 *
 * Focus:
 * - PCI discovery
 * - MMIO init + rings
 * - polled RX/TX path
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/mmu.h>
#include <drivers/pci.h>
#include <mm/pmm.h>
#include <net/net.h>
#include <net/e1000.h>
#include <obelisk/zig_net.h>

#define E1000_VENDOR_ID 0x8086

#define E1000_REG_CTRL    0x0000
#define E1000_REG_STATUS  0x0008
#define E1000_REG_EERD    0x0014
#define E1000_REG_ICR     0x00C0
#define E1000_REG_IMS     0x00D0
#define E1000_REG_RCTL    0x0100
#define E1000_REG_TCTL    0x0400
#define E1000_REG_TIPG    0x0410
#define E1000_REG_RDBAL   0x2800
#define E1000_REG_RDBAH   0x2804
#define E1000_REG_RDLEN   0x2808
#define E1000_REG_RDH     0x2810
#define E1000_REG_RDT     0x2818
#define E1000_REG_TDBAL   0x3800
#define E1000_REG_TDBAH   0x3804
#define E1000_REG_TDLEN   0x3808
#define E1000_REG_TDH     0x3810
#define E1000_REG_TDT     0x3818
#define E1000_REG_RAL0    0x5400
#define E1000_REG_RAH0    0x5404

#define E1000_CTRL_RST BIT(26)
#define E1000_CTRL_FD BIT(0)
#define E1000_CTRL_SLU BIT(6)
#define E1000_CTRL_ASDE BIT(5)
#define E1000_STATUS_LU BIT(1)
#define E1000_RAH_AV BIT(31)

#define E1000_RCTL_EN BIT(1)
#define E1000_RCTL_UPE BIT(3)
#define E1000_RCTL_MPE BIT(4)
#define E1000_RCTL_BAM BIT(15)
#define E1000_RCTL_SECRC BIT(26)
#define E1000_RCTL_BSIZE_2048 0

#define E1000_TCTL_EN BIT(1)
#define E1000_TCTL_PSP BIT(3)
#define E1000_TCTL_RTLC BIT(24)
#define E1000_TCTL_CT_SHIFT 4
#define E1000_TCTL_COLD_SHIFT 12

#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_MEM 0x2
#define PCI_COMMAND_BUS_MASTER 0x4

#define E1000_RX_RING_SIZE 32
#define E1000_TX_RING_SIZE 32
#define E1000_PKT_BUF_SIZE 2048
#define E1000_MMIO_MAP_SIZE 0x20000UL

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __packed;

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __packed;

struct e1000_state {
    bool present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t device_id;
    uint64_t mmio_phys;
    volatile uint8_t *mmio;
    uint8_t mac[6];
    uint64_t rx_ring_phys;
    uint64_t tx_ring_phys;
    struct e1000_rx_desc *rx_ring;
    struct e1000_tx_desc *tx_ring;
    uint64_t rx_buf_phys[E1000_RX_RING_SIZE];
    uint8_t *rx_buf_virt[E1000_RX_RING_SIZE];
    uint64_t tx_buf_phys[E1000_TX_RING_SIZE];
    uint8_t *tx_buf_virt[E1000_TX_RING_SIZE];
    uint32_t tx_tail;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_drops;
    uint64_t tx_timeouts;
    uint64_t poll_count;
};

static struct e1000_state g_e1000;
static int e1000_tx(void *ctx, const void *frame, size_t len);
static int e1000_poll(void *ctx);
static const struct net_device_ops g_e1000_ops = {
    .tx = e1000_tx,
    .poll = e1000_poll,
};

static bool e1000_mac_is_valid(const uint8_t mac[6]) {
    return zig_net_mac_is_valid(mac, 6) == 1;
}

static int e1000_map_mmio(struct e1000_state *s, uint64_t bar_phys) {
    uint64_t map_phys;
    uint64_t map_virt;
    int ret;

    if (!s) {
        return -EINVAL;
    }

    map_phys = ALIGN_DOWN(bar_phys, PAGE_SIZE);
    map_virt = (uint64_t)PHYS_TO_VIRT(map_phys);
    /*
     * Keep mapping flags conservative here: PTE_NX can fault with RSVD if NXE
     * is not enabled in EFER on this boot path.
     */
    ret = mmu_map_range(mmu_get_kernel_pt(), map_virt, map_phys, E1000_MMIO_MAP_SIZE,
                        PTE_WRITABLE | PTE_NOCACHE);
    if (ret < 0) {
        printk(KERN_WARNING "e1000: failed to map MMIO phys=0x%lx size=0x%lx (%d)\n",
               map_phys, (uint64_t)E1000_MMIO_MAP_SIZE, ret);
        return ret;
    }

    s->mmio_phys = bar_phys;
    s->mmio = (volatile uint8_t *)(map_virt + (bar_phys - map_phys));
    return 0;
}

static inline uint32_t e1000_read32(struct e1000_state *s, uint32_t reg) {
    return *(volatile uint32_t *)(s->mmio + reg);
}

static inline void e1000_write32(struct e1000_state *s, uint32_t reg, uint32_t value) {
    *(volatile uint32_t *)(s->mmio + reg) = value;
}

static bool e1000_wait_reset(struct e1000_state *s) {
    for (int i = 0; i < 100000; i++) {
        if ((e1000_read32(s, E1000_REG_CTRL) & E1000_CTRL_RST) == 0) {
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static bool e1000_device_id_supported(uint16_t id) {
    switch (id) {
        case 0x100E: /* 82540EM (QEMU default) */
        case 0x100F:
        case 0x1010:
        case 0x1011:
        case 0x1012:
        case 0x1013:
        case 0x1015:
        case 0x107C:
            return true;
        default:
            return false;
    }
}

static bool e1000_pci_match(const struct pci_device *d) {
    if (!d) return false;
    if (d->vendor_id != E1000_VENDOR_ID) return false;
    if (d->class_code != 0x02) return false;
    return e1000_device_id_supported(d->device_id);
}

static int e1000_alloc_buffers(struct e1000_state *s) {
    for (uint32_t i = 0; i < E1000_RX_RING_SIZE; i++) {
        uint64_t p = pmm_alloc_page();
        if (!p) return -ENOMEM;
        s->rx_buf_phys[i] = p;
        s->rx_buf_virt[i] = (uint8_t *)PHYS_TO_VIRT(p);
        memset(s->rx_buf_virt[i], 0, PAGE_SIZE);
    }
    for (uint32_t i = 0; i < E1000_TX_RING_SIZE; i++) {
        uint64_t p = pmm_alloc_page();
        if (!p) return -ENOMEM;
        s->tx_buf_phys[i] = p;
        s->tx_buf_virt[i] = (uint8_t *)PHYS_TO_VIRT(p);
        memset(s->tx_buf_virt[i], 0, PAGE_SIZE);
    }
    return 0;
}

static void e1000_read_mac(struct e1000_state *s) {
    uint32_t ral = e1000_read32(s, E1000_REG_RAL0);
    uint32_t rah = e1000_read32(s, E1000_REG_RAH0);
    s->mac[0] = (uint8_t)(ral & 0xFF);
    s->mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    s->mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    s->mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    s->mac[4] = (uint8_t)(rah & 0xFF);
    s->mac[5] = (uint8_t)((rah >> 8) & 0xFF);
}

static void e1000_program_mac(struct e1000_state *s) {
    uint32_t ral = ((uint32_t)s->mac[0]) |
                   ((uint32_t)s->mac[1] << 8) |
                   ((uint32_t)s->mac[2] << 16) |
                   ((uint32_t)s->mac[3] << 24);
    uint32_t rah = ((uint32_t)s->mac[4]) |
                   ((uint32_t)s->mac[5] << 8) |
                   E1000_RAH_AV;
    e1000_write32(s, E1000_REG_RAL0, ral);
    e1000_write32(s, E1000_REG_RAH0, rah);
}

static int e1000_setup_rings(struct e1000_state *s) {
    uint64_t ring_bytes = 0;
    if (zig_net_ring_bytes_ok(E1000_RX_RING_SIZE, sizeof(struct e1000_rx_desc),
                              16, PAGE_SIZE, &ring_bytes) != 0 ||
        zig_net_ring_bytes_ok(E1000_TX_RING_SIZE, sizeof(struct e1000_tx_desc),
                              16, PAGE_SIZE, &ring_bytes) != 0) {
        return -EINVAL;
    }
    s->rx_ring_phys = pmm_alloc_pages(1);
    s->tx_ring_phys = pmm_alloc_pages(1);
    if (!s->rx_ring_phys || !s->tx_ring_phys) {
        return -ENOMEM;
    }
    s->rx_ring = (struct e1000_rx_desc *)PHYS_TO_VIRT(s->rx_ring_phys);
    s->tx_ring = (struct e1000_tx_desc *)PHYS_TO_VIRT(s->tx_ring_phys);
    memset(s->rx_ring, 0, PAGE_SIZE);
    memset(s->tx_ring, 0, PAGE_SIZE);

    for (uint32_t i = 0; i < E1000_RX_RING_SIZE; i++) {
        s->rx_ring[i].addr = s->rx_buf_phys[i];
        s->rx_ring[i].status = 0;
    }
    for (uint32_t i = 0; i < E1000_TX_RING_SIZE; i++) {
        s->tx_ring[i].addr = s->tx_buf_phys[i];
        s->tx_ring[i].status = BIT(0); /* DD: descriptor free */
    }

    e1000_write32(s, E1000_REG_RDBAL, (uint32_t)(s->rx_ring_phys & 0xFFFFFFFFU));
    e1000_write32(s, E1000_REG_RDBAH, (uint32_t)(s->rx_ring_phys >> 32));
    e1000_write32(s, E1000_REG_RDLEN, E1000_RX_RING_SIZE * sizeof(struct e1000_rx_desc));
    e1000_write32(s, E1000_REG_RDH, 0);
    e1000_write32(s, E1000_REG_RDT, E1000_RX_RING_SIZE - 1);

    e1000_write32(s, E1000_REG_TDBAL, (uint32_t)(s->tx_ring_phys & 0xFFFFFFFFU));
    e1000_write32(s, E1000_REG_TDBAH, (uint32_t)(s->tx_ring_phys >> 32));
    e1000_write32(s, E1000_REG_TDLEN, E1000_TX_RING_SIZE * sizeof(struct e1000_tx_desc));
    e1000_write32(s, E1000_REG_TDH, 0);
    e1000_write32(s, E1000_REG_TDT, 0);
    s->tx_tail = 0;

    return 0;
}

static int e1000_tx(void *ctx, const void *frame, size_t len) {
    struct e1000_state *s = (struct e1000_state *)ctx;
    struct e1000_tx_desc *desc;
    uint32_t idx;
    if (!s || !s->present || !frame || len == 0) {
        return -EINVAL;
    }
    if (zig_net_frame_len_ok((uint64_t)len, 1, E1000_PKT_BUF_SIZE) != 0) {
        return -EMSGSIZE;
    }

    idx = s->tx_tail;
    desc = &s->tx_ring[idx];
    if ((desc->status & BIT(0)) == 0) {
        return -EAGAIN;
    }

    memcpy(s->tx_buf_virt[idx], frame, len);
    desc->length = (uint16_t)len;
    desc->cmd = (uint8_t)(BIT(0) | BIT(1) | BIT(3)); /* EOP|IFCS|RS */
    desc->status = 0;

    s->tx_tail = (idx + 1) % E1000_TX_RING_SIZE;
    e1000_write32(s, E1000_REG_TDT, s->tx_tail);
    for (int spin = 0; spin < 200000; spin++) {
        if (desc->status & BIT(0)) { /* DD */
            break;
        }
        __asm__ volatile("pause");
    }
    if ((desc->status & BIT(0)) == 0) {
        s->tx_timeouts++;
        if ((s->tx_packets % 64) == 0) {
            uint32_t tdh = e1000_read32(s, E1000_REG_TDH);
            uint32_t tdt = e1000_read32(s, E1000_REG_TDT);
            uint32_t status = e1000_read32(s, E1000_REG_STATUS);
            printk(KERN_WARNING "e1000: tx timeout idx=%u tdh=%u tdt=%u status=0x%x\n",
                   idx, tdh, tdt, status);
        }
        return -EIO;
    }
    s->tx_packets++;
    s->tx_bytes += len;
    return 0;
}

static int e1000_poll(void *ctx) {
    struct e1000_state *s = (struct e1000_state *)ctx;
    uint32_t rdh;
    uint32_t rdt;
    if (!s || !s->present) {
        return -ENODEV;
    }
    s->poll_count++;

    rdh = e1000_read32(s, E1000_REG_RDH);
    rdt = e1000_read32(s, E1000_REG_RDT);

    /* Consume all completed descriptors between tail and head. */
    while (1) {
        uint32_t next = (rdt + 1) % E1000_RX_RING_SIZE;
        struct e1000_rx_desc *d = &s->rx_ring[next];
        if ((d->status & BIT(0)) == 0) { /* DD */
            break;
        }

        if ((d->status & BIT(1)) && d->length > 0 && d->length <= E1000_PKT_BUF_SIZE) { /* EOP */
            s->rx_packets++;
            s->rx_bytes += d->length;
            net_rx_frame(s->rx_buf_virt[next], d->length);
        } else {
            s->rx_drops++;
        }

        d->status = 0;
        e1000_write32(s, E1000_REG_RDT, next);
        rdt = next;
    }

    if ((s->rx_packets % 128) == 0 && s->rx_packets != 0) {
        printk(KERN_INFO "e1000: rx=%lu tx=%lu drop=%lu bytes_rx=%lu bytes_tx=%lu rdh=%u\n",
               s->rx_packets, s->tx_packets, s->rx_drops, s->rx_bytes, s->tx_bytes, rdh);
    }
    return 0;
}

void e1000_init(void) {
    size_t count = pci_device_count();

    if (net_is_ready()) {
        return;
    }

    memset(&g_e1000, 0, sizeof(g_e1000));
    for (size_t i = 0; i < count; i++) {
        const struct pci_device *d = pci_get_device(i);
        uint32_t bar0;
        uint64_t bar_phys;
        uint16_t cmd;
        int ret;

        if (!e1000_pci_match(d)) {
            continue;
        }
        bar0 = d->bar[0];
        if (bar0 & 0x1U) {
            continue; /* IO BAR not supported in this path */
        }
        bar_phys = (uint64_t)(bar0 & ~0xFU);
        if ((bar0 & 0x6U) == 0x4U) {
            bar_phys |= ((uint64_t)d->bar[1] << 32);
        }
        if (bar_phys == 0) {
            printk(KERN_WARNING "e1000: skip %04x at %02x:%02x.%u: invalid BAR0\n",
                   d->device_id, d->bus, d->slot, d->function);
            continue;
        }

        g_e1000.present = true;
        g_e1000.bus = d->bus;
        g_e1000.slot = d->slot;
        g_e1000.function = d->function;
        g_e1000.device_id = d->device_id;
        ret = e1000_map_mmio(&g_e1000, bar_phys);
        if (ret < 0) {
            g_e1000.present = false;
            continue;
        }

        cmd = pci_config_read16(d->bus, d->slot, d->function, 0x04);
        cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUS_MASTER);
        pci_config_write16(d->bus, d->slot, d->function, 0x04, cmd);

        e1000_write32(&g_e1000, E1000_REG_CTRL, e1000_read32(&g_e1000, E1000_REG_CTRL) | E1000_CTRL_RST);
        if (!e1000_wait_reset(&g_e1000)) {
            printk(KERN_WARNING "e1000: reset timeout on %02x:%02x.%u\n",
                   d->bus, d->slot, d->function);
            g_e1000.present = false;
            continue;
        }
        e1000_write32(&g_e1000, E1000_REG_CTRL,
                      e1000_read32(&g_e1000, E1000_REG_CTRL) |
                          E1000_CTRL_FD | E1000_CTRL_SLU | E1000_CTRL_ASDE);
        (void)e1000_read32(&g_e1000, E1000_REG_ICR);
        e1000_write32(&g_e1000, E1000_REG_IMS, 0);

        ret = e1000_alloc_buffers(&g_e1000);
        if (ret < 0) {
            printk(KERN_WARNING "e1000: buffer allocation failed: %d\n", ret);
            g_e1000.present = false;
            continue;
        }
        ret = e1000_setup_rings(&g_e1000);
        if (ret < 0) {
            printk(KERN_WARNING "e1000: ring setup failed: %d\n", ret);
            g_e1000.present = false;
            continue;
        }

        e1000_read_mac(&g_e1000);
        if (!e1000_mac_is_valid(g_e1000.mac)) {
            /* Fallback deterministic local MAC if EEPROM/MAC read is unavailable. */
            g_e1000.mac[0] = 0x02;
            g_e1000.mac[1] = 0x4F;
            g_e1000.mac[2] = 0x42;
            g_e1000.mac[3] = 0x45;
            g_e1000.mac[4] = 0x10;
            g_e1000.mac[5] = 0x00;
            printk(KERN_WARNING "e1000: invalid EEPROM MAC; using local fallback MAC\n");
        }
        e1000_program_mac(&g_e1000);

        e1000_write32(&g_e1000, E1000_REG_RCTL,
                      E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE |
                          E1000_RCTL_BAM | E1000_RCTL_SECRC | E1000_RCTL_BSIZE_2048);
        e1000_write32(&g_e1000, E1000_REG_TIPG, 0x0060200A);
        e1000_write32(&g_e1000, E1000_REG_TCTL,
                      E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_RTLC |
                          (0x10U << E1000_TCTL_CT_SHIFT) |
                          (0x40U << E1000_TCTL_COLD_SHIFT));

        printk(KERN_INFO "e1000: device %04x at %02x:%02x.%u mmio=0x%lx link=%s\n",
               g_e1000.device_id, g_e1000.bus, g_e1000.slot, g_e1000.function,
               g_e1000.mmio_phys,
               (e1000_read32(&g_e1000, E1000_REG_STATUS) & E1000_STATUS_LU) ? "up" : "down");
        printk(KERN_INFO "e1000: mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
               g_e1000.mac[0], g_e1000.mac[1], g_e1000.mac[2],
               g_e1000.mac[3], g_e1000.mac[4], g_e1000.mac[5]);

        ret = net_register_device("e1000", g_e1000.mac, &g_e1000_ops, &g_e1000);
        if (ret < 0) {
            printk(KERN_WARNING "e1000: net registration failed: %d\n", ret);
            g_e1000.present = false;
            continue;
        }
        return;
    }

    printk(KERN_INFO "e1000: no supported Intel e1000-class NIC found\n");
}
