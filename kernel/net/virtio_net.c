/*
 * Obelisk OS - Virtio Net (Phase 1 Bring-up)
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <drivers/pci.h>
#include <mm/pmm.h>
#include <net/net.h>
#include <net/virtio_net.h>
#include <obelisk/zig_net.h>

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_NET_DEV_LEGACY 0x1000
#define VIRTIO_NET_DEV_MODERN 0x1041

/* Legacy virtio-pci I/O register layout. */
#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_NUM 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_DEVICE_STATUS 0x12
#define VIRTIO_PCI_ISR_STATUS 0x13
#define VIRTIO_PCI_CONFIG_BASE 0x14

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01
#define VIRTIO_STATUS_DRIVER 0x02
#define VIRTIO_STATUS_DRIVER_OK 0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08

#define VIRTQ_ALIGN 4096
#define VIRTQ_INDEX_RX 0
#define VIRTQ_INDEX_TX 1
#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_MEM 0x2
#define PCI_COMMAND_BUS_MASTER 0x4

#define NET_BUFFER_SIZE 2048
#define VIRTIO_MMIO_MAP_SIZE 0x1000

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __packed;

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __packed;

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __packed;

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
} __packed;

struct virtio_legacy_queue {
    uint16_t qsize;
    uint64_t mem_phys;
    void *mem_virt;
    size_t mem_size;
    size_t mem_pages;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    uint16_t last_used_idx;
    uint64_t *buf_phys;
    uint8_t **buf_virt;
    uint8_t *desc_busy;
};

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __packed;

struct virtio_net_state {
    bool present;
    bool mmio;                       /* BAR0 access mode */
    volatile uint8_t *mmio_base;    /* Mapped BAR0 base for MMIO */
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t device_id;
    uint16_t io_base;
    uint8_t irq_line;
    uint8_t mac[6];
    uint32_t host_features;
    bool link_up;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t irq_count;
    struct virtio_legacy_queue rxq;
    struct virtio_legacy_queue txq;
};

static struct virtio_net_state g_vnet;
static int virtio_net_poll(void *ctx);
static int virtio_net_tx_cb(void *ctx, const void *frame, size_t len);
static const struct net_device_ops g_vnet_ops = {
    .tx = virtio_net_tx_cb,
    .poll = virtio_net_poll,
};

static bool vnet_mac_is_valid(const uint8_t mac[6]) {
    return zig_net_mac_is_valid(mac, 6) == 1;
}

static inline uint8_t vnet_read8(struct virtio_net_state *s, uint32_t reg) {
    if (!s) return 0;
    if (s->mmio) {
        return *(volatile uint8_t *)(s->mmio_base + reg);
    }
    return inb((uint16_t)(s->io_base + reg));
}

static inline void vnet_write8(struct virtio_net_state *s, uint32_t reg, uint8_t value) {
    if (!s) return;
    if (s->mmio) {
        *(volatile uint8_t *)(s->mmio_base + reg) = value;
        return;
    }
    outb((uint16_t)(s->io_base + reg), value);
}

static inline uint16_t vnet_read16(struct virtio_net_state *s, uint32_t reg) {
    if (!s) return 0;
    if (s->mmio) {
        return *(volatile uint16_t *)(s->mmio_base + reg);
    }
    return inw((uint16_t)(s->io_base + reg));
}

static inline void vnet_write16(struct virtio_net_state *s, uint32_t reg, uint16_t value) {
    if (!s) return;
    if (s->mmio) {
        *(volatile uint16_t *)(s->mmio_base + reg) = value;
        return;
    }
    outw((uint16_t)(s->io_base + reg), value);
}

static inline uint32_t vnet_read32(struct virtio_net_state *s, uint32_t reg) {
    if (!s) return 0;
    if (s->mmio) {
        return *(volatile uint32_t *)(s->mmio_base + reg);
    }
    return inl((uint16_t)(s->io_base + reg));
}

static inline void vnet_write32(struct virtio_net_state *s, uint32_t reg, uint32_t value) {
    if (!s) return;
    if (s->mmio) {
        *(volatile uint32_t *)(s->mmio_base + reg) = value;
        return;
    }
    outl((uint16_t)(s->io_base + reg), value);
}

static size_t virtq_total_bytes(uint16_t qsize) {
    size_t desc_bytes = (size_t)qsize * sizeof(struct vring_desc);
    size_t avail_bytes = sizeof(uint16_t) * (3 + (size_t)qsize);
    size_t used_bytes = sizeof(uint16_t) * 3 + (size_t)qsize * sizeof(struct vring_used_elem);
    size_t used_offset = ALIGN_UP(desc_bytes + avail_bytes, VIRTQ_ALIGN);
    return used_offset + used_bytes;
}

static int virtio_legacy_queue_setup(struct virtio_net_state *s, uint16_t qindex, struct virtio_legacy_queue *q) {
    uint16_t qmax;
    size_t used_offset;
    uint64_t desc_bytes = 0;

    vnet_write16(s, VIRTIO_PCI_QUEUE_SEL, qindex);
    qmax = vnet_read16(s, VIRTIO_PCI_QUEUE_NUM);
    if (qmax == 0) {
        return -ENOENT;
    }
    if (zig_net_ring_bytes_ok(qmax, sizeof(struct vring_desc), 16, (8U * PAGE_SIZE), &desc_bytes) != 0) {
        return -EINVAL;
    }

    q->qsize = qmax;
    q->mem_size = virtq_total_bytes(qmax);
    q->mem_pages = ALIGN_UP(q->mem_size, PAGE_SIZE) / PAGE_SIZE;
    q->mem_phys = pmm_alloc_pages(q->mem_pages);
    if (!q->mem_phys) {
        return -ENOMEM;
    }
    q->mem_virt = PHYS_TO_VIRT(q->mem_phys);
    memset(q->mem_virt, 0, q->mem_pages * PAGE_SIZE);

    q->desc = (struct vring_desc *)q->mem_virt;
    q->avail = (struct vring_avail *)((uint8_t *)q->mem_virt + ((size_t)qmax * sizeof(struct vring_desc)));
    used_offset = ALIGN_UP(((size_t)qmax * sizeof(struct vring_desc)) +
                           (sizeof(uint16_t) * (3 + (size_t)qmax)),
                           VIRTQ_ALIGN);
    q->used = (struct vring_used *)((uint8_t *)q->mem_virt + used_offset);

    /* Legacy queue address is PFN of queue memory. */
    vnet_write32(s, VIRTIO_PCI_QUEUE_PFN, (uint32_t)(q->mem_phys >> PAGE_SHIFT));
    return 0;
}

static int virtio_queue_alloc_buffers(struct virtio_legacy_queue *q) {
    q->buf_phys = kcalloc(q->qsize, sizeof(uint64_t));
    q->buf_virt = kcalloc(q->qsize, sizeof(uint8_t *));
    q->desc_busy = kcalloc(q->qsize, sizeof(uint8_t));
    if (!q->buf_phys || !q->buf_virt || !q->desc_busy) {
        return -ENOMEM;
    }

    for (uint16_t i = 0; i < q->qsize; i++) {
        uint64_t p = pmm_alloc_page();
        if (!p) {
            return -ENOMEM;
        }
        q->buf_phys[i] = p;
        q->buf_virt[i] = (uint8_t *)PHYS_TO_VIRT(p);
        memset(q->buf_virt[i], 0, PAGE_SIZE);
    }
    return 0;
}

static void virtio_queue_push_avail(struct virtio_legacy_queue *q, uint16_t desc_idx) {
    uint16_t slot = q->avail->idx % q->qsize;
    q->avail->ring[slot] = desc_idx;
    wmb();
    q->avail->idx++;
}

static int virtio_rx_prime(struct virtio_net_state *s) {
    struct virtio_legacy_queue *q = &s->rxq;
    for (uint16_t i = 0; i < q->qsize; i++) {
        q->desc[i].addr = q->buf_phys[i];
        q->desc[i].len = NET_BUFFER_SIZE;
        q->desc[i].flags = VIRTQ_DESC_F_WRITE;
        q->desc[i].next = 0;
        q->desc_busy[i] = 1;
        virtio_queue_push_avail(q, i);
    }
    vnet_write16(s, VIRTIO_PCI_QUEUE_NOTIFY, VIRTQ_INDEX_RX);
    return 0;
}

static int virtio_tx_prepare(struct virtio_net_state *s) {
    struct virtio_legacy_queue *q = &s->txq;
    for (uint16_t i = 0; i < q->qsize; i++) {
        struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)q->buf_virt[i];
        memset(hdr, 0, sizeof(*hdr));
        q->desc[i].addr = q->buf_phys[i];
        q->desc[i].len = sizeof(*hdr);
        q->desc[i].flags = 0;
        q->desc[i].next = 0;
        q->desc_busy[i] = 0;
    }
    return 0;
}

static int virtio_tx_submit(struct virtio_net_state *s, const void *payload, size_t len) {
    struct virtio_legacy_queue *q = &s->txq;
    size_t frame_len;
    uint16_t desc_idx = UINT16_MAX;

    if (!payload || len == 0) {
        return -EINVAL;
    }

    frame_len = sizeof(struct virtio_net_hdr) + len;
    if (zig_net_frame_len_ok((uint64_t)frame_len, sizeof(struct virtio_net_hdr), NET_BUFFER_SIZE) != 0) {
        return -EMSGSIZE;
    }

    for (uint16_t i = 0; i < q->qsize; i++) {
        if (!q->desc_busy[i]) {
            desc_idx = i;
            break;
        }
    }
    if (desc_idx == UINT16_MAX) {
        return -EAGAIN;
    }

    memset(q->buf_virt[desc_idx], 0, sizeof(struct virtio_net_hdr));
    memcpy(q->buf_virt[desc_idx] + sizeof(struct virtio_net_hdr), payload, len);
    q->desc[desc_idx].len = (uint32_t)frame_len;
    q->desc_busy[desc_idx] = 1;
    virtio_queue_push_avail(q, desc_idx);
    vnet_write16(s, VIRTIO_PCI_QUEUE_NOTIFY, VIRTQ_INDEX_TX);
    return 0;
}

static int virtio_net_tx_cb(void *ctx, const void *frame, size_t len) {
    return virtio_tx_submit((struct virtio_net_state *)ctx, frame, len);
}

static void virtio_rx_recycle(struct virtio_net_state *s, uint16_t desc_idx) {
    struct virtio_legacy_queue *q = &s->rxq;
    q->desc[desc_idx].addr = q->buf_phys[desc_idx];
    q->desc[desc_idx].len = NET_BUFFER_SIZE;
    q->desc[desc_idx].flags = VIRTQ_DESC_F_WRITE;
    q->desc[desc_idx].next = 0;
    q->desc_busy[desc_idx] = 1;
    virtio_queue_push_avail(q, desc_idx);
}

static void virtio_handle_rx_used(struct virtio_net_state *s) {
    struct virtio_legacy_queue *q = &s->rxq;
    while (q->last_used_idx != q->used->idx) {
        uint16_t slot = q->last_used_idx % q->qsize;
        struct vring_used_elem *e = &q->used->ring[slot];
        uint16_t id = (uint16_t)e->id;
        uint32_t len = e->len;

        if (id < q->qsize) {
            size_t payload_len = 0;
            uint8_t *payload = NULL;
            if (len > sizeof(struct virtio_net_hdr)) {
                payload_len = len - sizeof(struct virtio_net_hdr);
                payload = q->buf_virt[id] + sizeof(struct virtio_net_hdr);
            }
            s->rx_packets++;
            s->rx_bytes += payload_len;
            if (payload && payload_len > 0) {
                net_rx_frame(payload, payload_len);
            }
            virtio_rx_recycle(s, id);
        }
        q->last_used_idx++;
    }
}

static void virtio_handle_tx_used(struct virtio_net_state *s) {
    struct virtio_legacy_queue *q = &s->txq;
    while (q->last_used_idx != q->used->idx) {
        uint16_t slot = q->last_used_idx % q->qsize;
        struct vring_used_elem *e = &q->used->ring[slot];
        uint16_t id = (uint16_t)e->id;
        if (id < q->qsize && q->desc_busy[id]) {
            size_t len = q->desc[id].len;
            if (len >= sizeof(struct virtio_net_hdr)) {
                s->tx_packets++;
                s->tx_bytes += (len - sizeof(struct virtio_net_hdr));
            }
            q->desc[id].len = sizeof(struct virtio_net_hdr);
            q->desc_busy[id] = 0;
        }
        q->last_used_idx++;
    }
}

static void virtio_net_irq(uint8_t irq, struct cpu_regs *regs, void *ctx) {
    struct virtio_net_state *s = (struct virtio_net_state *)ctx;
    uint8_t isr;
    (void)irq;
    (void)regs;
    if (!s || !s->present || !s->link_up) {
        return;
    }

    isr = vnet_read8(s, VIRTIO_PCI_ISR_STATUS);
    if (isr == 0) {
        return;
    }

    s->irq_count++;
    if (isr & 0x1) {
        virtio_handle_rx_used(s);
        virtio_handle_tx_used(s);
        vnet_write16(s, VIRTIO_PCI_QUEUE_NOTIFY, VIRTQ_INDEX_RX);
    }
    if (isr & 0x2) {
        printk(KERN_INFO "virtio-net: config change interrupt\n");
    }

    if ((s->irq_count % 64) == 0) {
        printk(KERN_INFO "virtio-net: irq=%lu rx_pkts=%lu tx_pkts=%lu rx_bytes=%lu tx_bytes=%lu\n",
               s->irq_count, s->rx_packets, s->tx_packets, s->rx_bytes, s->tx_bytes);
    }
}

static int virtio_net_poll(void *ctx) {
    struct virtio_net_state *s = (struct virtio_net_state *)ctx;
    uint8_t isr;
    if (!s || !s->present || !s->link_up) {
        return -ENODEV;
    }
    isr = vnet_read8(s, VIRTIO_PCI_ISR_STATUS);
    if (isr & 0x1) {
        virtio_handle_rx_used(s);
        virtio_handle_tx_used(s);
        vnet_write16(s, VIRTIO_PCI_QUEUE_NOTIFY, VIRTQ_INDEX_RX);
    }
    return 0;
}

static bool virtio_net_pci_match(const struct pci_device *d) {
    if (!d) {
        return false;
    }
    if (d->vendor_id != VIRTIO_VENDOR_ID) {
        return false;
    }
    if (d->device_id == VIRTIO_NET_DEV_LEGACY || d->device_id == VIRTIO_NET_DEV_MODERN) {
        return true;
    }
    return false;
}

static int virtio_net_legacy_init(const struct pci_device *d) {
    uint32_t bar0;
    uint16_t io_base = 0;
    uint64_t bar0_phys = 0;
    uint64_t map_phys = 0;
    uint64_t map_virt = 0;
    int map_ret;
    int ret;
    uint16_t pci_cmd;

    if (!d) {
        return -EINVAL;
    }

    bar0 = d->bar[0];
    if ((bar0 & 0x1U) != 0) {
        /* Legacy I/O BAR layout. */
        io_base = (uint16_t)(bar0 & ~0x3U);
        if (io_base == 0) {
            return -ENODEV;
        }
    } else {
        /* MMIO BAR layout (common for virtio-net-pci in VMs). */
        bar0_phys = (uint64_t)(bar0 & ~0x3U);
        if (bar0_phys == 0) {
            return -ENODEV;
        }
        map_phys = ALIGN_DOWN(bar0_phys, PAGE_SIZE);
        map_virt = (uint64_t)PHYS_TO_VIRT(map_phys);
        map_ret = mmu_map_range(mmu_get_kernel_pt(), map_virt, map_phys,
                                VIRTIO_MMIO_MAP_SIZE,
                                PTE_WRITABLE | PTE_NOCACHE);
        if (map_ret < 0) {
            printk(KERN_WARNING "virtio-net: failed to map MMIO BAR0 phys=0x%lx size=0x%lx (%d)\n",
                   bar0_phys, (uint64_t)VIRTIO_MMIO_MAP_SIZE, map_ret);
            return map_ret;
        }
    }

    memset(&g_vnet, 0, sizeof(g_vnet));
    g_vnet.present = true;
    g_vnet.mmio = ((bar0 & 0x1U) == 0);
    g_vnet.bus = d->bus;
    g_vnet.slot = d->slot;
    g_vnet.function = d->function;
    g_vnet.device_id = d->device_id;
    g_vnet.io_base = io_base;
    if (g_vnet.mmio) {
        g_vnet.mmio_base = (volatile uint8_t *)(map_virt + (bar0_phys - map_phys));
    } else {
        g_vnet.mmio_base = NULL;
    }
    g_vnet.irq_line = d->interrupt_line;
    g_vnet.link_up = false;

    pci_cmd = pci_config_read16(d->bus, d->slot, d->function, 0x04);
    pci_cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUS_MASTER);
    pci_config_write16(d->bus, d->slot, d->function, 0x04, pci_cmd);

    vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS, 0);
    vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    g_vnet.host_features = vnet_read32(&g_vnet, VIRTIO_PCI_HOST_FEATURES);
    vnet_write32(&g_vnet, VIRTIO_PCI_GUEST_FEATURES, 0);
    vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    ret = virtio_legacy_queue_setup(&g_vnet, VIRTQ_INDEX_RX, &g_vnet.rxq);
    if (ret < 0) {
        printk(KERN_WARNING "virtio-net: RX queue setup failed: %d\n", ret);
        vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS, 0);
        return ret;
    }

    ret = virtio_legacy_queue_setup(&g_vnet, VIRTQ_INDEX_TX, &g_vnet.txq);
    if (ret < 0) {
        printk(KERN_WARNING "virtio-net: TX queue setup failed: %d\n", ret);
        vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS, 0);
        return ret;
    }

    ret = virtio_queue_alloc_buffers(&g_vnet.rxq);
    if (ret < 0) {
        printk(KERN_WARNING "virtio-net: RX buffer allocation failed: %d\n", ret);
        vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS, 0);
        return ret;
    }
    ret = virtio_queue_alloc_buffers(&g_vnet.txq);
    if (ret < 0) {
        printk(KERN_WARNING "virtio-net: TX buffer allocation failed: %d\n", ret);
        vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS, 0);
        return ret;
    }
    ret = virtio_rx_prime(&g_vnet);
    if (ret < 0) {
        printk(KERN_WARNING "virtio-net: RX queue prime failed: %d\n", ret);
        vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS, 0);
        return ret;
    }
    ret = virtio_tx_prepare(&g_vnet);
    if (ret < 0) {
        printk(KERN_WARNING "virtio-net: TX queue prepare failed: %d\n", ret);
        vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS, 0);
        return ret;
    }

    for (int i = 0; i < 6; i++) {
        g_vnet.mac[i] = vnet_read8(&g_vnet, VIRTIO_PCI_CONFIG_BASE + i);
    }
    if (!vnet_mac_is_valid(g_vnet.mac)) {
        g_vnet.mac[0] = 0x02;
        g_vnet.mac[1] = 0x56;
        g_vnet.mac[2] = 0x49;
        g_vnet.mac[3] = 0x52;
        g_vnet.mac[4] = 0x54;
        g_vnet.mac[5] = (uint8_t)g_vnet.device_id;
        printk(KERN_WARNING "virtio-net: invalid device MAC; using local fallback MAC\n");
    }

    if (g_vnet.irq_line < 16) {
        ret = irq_register_handler(g_vnet.irq_line, virtio_net_irq, &g_vnet);
        if (ret == 0) {
            irq_enable(g_vnet.irq_line);
        } else {
            printk(KERN_WARNING "virtio-net: failed to register IRQ %u handler (%d)\n", g_vnet.irq_line, ret);
        }
    } else {
        printk(KERN_WARNING "virtio-net: IRQ line %u is outside PIC range; using polling-only behavior\n",
               g_vnet.irq_line);
    }

    (void)vnet_read8(&g_vnet, VIRTIO_PCI_ISR_STATUS);
    vnet_write8(&g_vnet, VIRTIO_PCI_DEVICE_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK |
                 VIRTIO_STATUS_DRIVER_OK);
    g_vnet.link_up = true;

    printk(KERN_INFO
           "virtio-net: legacy device ready at %02x:%02x.%u io=0x%x mmio=%u irq=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           d->bus, d->slot, d->function, io_base, g_vnet.mmio, g_vnet.irq_line,
           g_vnet.mac[0], g_vnet.mac[1], g_vnet.mac[2],
           g_vnet.mac[3], g_vnet.mac[4], g_vnet.mac[5]);
    printk(KERN_INFO "virtio-net: queues rx=%u tx=%u host_features=0x%x\n",
           g_vnet.rxq.qsize, g_vnet.txq.qsize, g_vnet.host_features);
    ret = net_register_device("virtio0", g_vnet.mac, &g_vnet_ops, &g_vnet);
    if (ret < 0) {
        printk(KERN_WARNING "virtio-net: failed to register with net core: %d\n", ret);
    }
    /* Fire one tiny self-test TX frame to validate descriptor kick path. */
    {
        static const uint8_t probe[14] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                          0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
                                          0x08, 0x06};
        ret = virtio_tx_submit(&g_vnet, probe, sizeof(probe));
        if (ret == 0) {
            printk(KERN_INFO "virtio-net: tx probe frame submitted\n");
        } else {
            printk(KERN_WARNING "virtio-net: tx probe submit failed: %d\n", ret);
        }
    }
    return 0;
}

void virtio_net_init(void) {
    size_t count = pci_device_count();
    bool matched = false;
    bool other_nic_seen = false;

    printk(KERN_INFO "virtio-net: probing PCI devices...\n");
    for (size_t i = 0; i < count; i++) {
        const struct pci_device *d = pci_get_device(i);
        if (d && d->class_code == 0x02 && d->vendor_id != VIRTIO_VENDOR_ID) {
            other_nic_seen = true;
        }
        if (!virtio_net_pci_match(d)) {
            continue;
        }

        matched = true;
        if (d->device_id == VIRTIO_NET_DEV_MODERN) {
            printk(KERN_WARNING
                   "virtio-net: found modern device at %02x:%02x.%u (phase 1 supports legacy transport first)\n",
                   d->bus, d->slot, d->function);
        }

        if (virtio_net_legacy_init(d) == 0) {
            return;
        }
    }

    if (!matched) {
        printk(KERN_INFO "virtio-net: no compatible device found\n");
        if (other_nic_seen) {
            printk(KERN_WARNING "virtio-net: non-virtio NIC(s) detected; add native driver for bare-metal NIC support\n");
        }
    } else {
        printk(KERN_WARNING "virtio-net: compatible device seen, but initialization failed\n");
    }
}
