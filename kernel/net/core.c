/*
 * Obelisk OS - Minimal Network Core
 * From Axioms, Order.
 *
 * Phase 3 scope:
 * - Ethernet parsing
 * - ARP request/reply
 * - IPv4 + ICMP echo reply
 * - Single registered NIC path
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <net/net.h>

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IPV4 0x0800
#define ARP_HTYPE_ETHERNET 1
#define ARP_PTYPE_IPV4 0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2
#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_UDP 17
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define UDP_PORT_DHCP_SERVER 67
#define UDP_PORT_DHCP_CLIENT 68
#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY 2
#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_OPT_MSG_TYPE 53
#define DHCP_OPT_SUBNET_MASK 1
#define DHCP_OPT_ROUTER 3
#define DHCP_OPT_REQ_IP 50
#define DHCP_OPT_SERVER_ID 54
#define DHCP_OPT_PARAM_REQ 55
#define DHCP_OPT_END 255
#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER 2
#define DHCP_MSG_REQUEST 3
#define DHCP_MSG_ACK 5
#define DHCP_RETRY_TICKS 200
#define DHCP_MAX_RETRIES 5
#define NET_ICMP_EVENT_RING 32
#define NET_UDP_EVENT_RING 32

struct eth_hdr {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} __packed;

struct arp_hdr {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} __packed;

struct ipv4_hdr {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint8_t saddr[4];
    uint8_t daddr[4];
} __packed;

struct icmp_echo_hdr {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t seq;
} __packed;

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __packed;

struct dhcp_packet {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
} __packed;

struct net_dev {
    bool present;
    char name[16];
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    const struct net_device_ops *ops;
    void *ctx;
    uint64_t rx_frames;
    uint64_t tx_frames;
    uint64_t rx_drop;
    uint64_t arp_rx;
    uint64_t ipv4_rx;
    uint64_t icmp_echo_rx;
    uint64_t udp_rx;
    uint64_t dhcp_events;
};

struct arp_cache_entry {
    bool valid;
    uint8_t ip[4];
    uint8_t mac[6];
};

static struct net_dev g_dev;
static struct arp_cache_entry g_arp;
static uint64_t g_tick_count;
static struct net_icmp_event g_icmp_events[NET_ICMP_EVENT_RING];
static uint32_t g_icmp_event_head;
static uint32_t g_icmp_event_tail;
static struct net_udp_event g_udp_events[NET_UDP_EVENT_RING];
static uint32_t g_udp_event_head;
static uint32_t g_udp_event_tail;
static int send_arp_request(const uint8_t target_ip[4]);

enum dhcp_state {
    DHCP_DISABLED = 0,
    DHCP_DISCOVER_SENT,
    DHCP_REQUEST_SENT,
    DHCP_BOUND,
    DHCP_FAILED,
};

struct dhcp_client_state {
    enum dhcp_state state;
    uint32_t xid;
    uint32_t last_event_tick;
    uint32_t retries;
    uint8_t offered_ip[4];
    uint8_t offered_mask[4];
    uint8_t offered_router[4];
    uint8_t server_id[4];
};

static struct dhcp_client_state g_dhcp;

static uint16_t bswap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FFU) << 24) |
           ((v & 0x0000FF00U) << 8) |
           ((v & 0x00FF0000U) >> 8) |
           ((v & 0xFF000000U) >> 24);
}

static uint16_t htons(uint16_t v) { return bswap16(v); }
static uint16_t ntohs(uint16_t v) { return bswap16(v); }
static uint32_t htonl(uint32_t v) { return bswap32(v); }
static uint32_t ntohl(uint32_t v) { return bswap32(v); }

static bool ip_in_same_subnet(const uint8_t a[4], const uint8_t b[4], const uint8_t mask[4]) {
    return (a[0] & mask[0]) == (b[0] & mask[0]) &&
           (a[1] & mask[1]) == (b[1] & mask[1]) &&
           (a[2] & mask[2]) == (b[2] & mask[2]) &&
           (a[3] & mask[3]) == (b[3] & mask[3]);
}

static bool ip_equal(const uint8_t a[4], const uint8_t b[4]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static void mac_copy(uint8_t dst[6], const uint8_t src[6]) {
    for (int i = 0; i < 6; i++) {
        dst[i] = src[i];
    }
}

static uint16_t ip_checksum(const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len) {
        sum += ((uint32_t)p[0] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFF);
}

static int net_tx_raw(const uint8_t *frame, size_t len) {
    int ret;
    if (!g_dev.present || !g_dev.ops || !g_dev.ops->tx) {
        return -ENODEV;
    }
    ret = g_dev.ops->tx(g_dev.ctx, frame, len);
    if (ret == 0) {
        g_dev.tx_frames++;
    }
    return ret;
}

static void udp_event_push(const uint8_t src_ip[4], uint16_t src_port, uint16_t dst_port,
                           const uint8_t *payload, size_t payload_len) {
    uint32_t next = (g_udp_event_head + 1U) % NET_UDP_EVENT_RING;
    struct net_udp_event *slot;
    if (next == g_udp_event_tail) {
        g_udp_event_tail = (g_udp_event_tail + 1U) % NET_UDP_EVENT_RING;
    }
    slot = &g_udp_events[g_udp_event_head];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->src_ip, src_ip, 4);
    slot->src_port = src_port;
    slot->dst_port = dst_port;
    if (payload_len > sizeof(slot->payload)) {
        payload_len = sizeof(slot->payload);
    }
    memcpy(slot->payload, payload, payload_len);
    slot->payload_len = payload_len;
    g_udp_event_head = next;
}

static void icmp_event_push(const uint8_t src_ip[4], const uint8_t *packet, size_t packet_len) {
    uint32_t next = (g_icmp_event_head + 1U) % NET_ICMP_EVENT_RING;
    struct net_icmp_event *slot;
    if (next == g_icmp_event_tail) {
        /* Drop oldest when full. */
        g_icmp_event_tail = (g_icmp_event_tail + 1U) % NET_ICMP_EVENT_RING;
    }
    slot = &g_icmp_events[g_icmp_event_head];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->src_ip, src_ip, 4);
    if (packet_len > sizeof(slot->packet)) {
        packet_len = sizeof(slot->packet);
    }
    memcpy(slot->packet, packet, packet_len);
    slot->packet_len = packet_len;
    g_icmp_event_head = next;
}

int net_recv_icmp_echo_event(struct net_icmp_event *out_event) {
    if (!out_event) {
        return -EINVAL;
    }
    if (g_icmp_event_head == g_icmp_event_tail) {
        return -EAGAIN;
    }
    *out_event = g_icmp_events[g_icmp_event_tail];
    g_icmp_event_tail = (g_icmp_event_tail + 1U) % NET_ICMP_EVENT_RING;
    return 0;
}

int net_recv_udp_event(uint16_t local_port, struct net_udp_event *out_event) {
    uint32_t idx;
    if (!out_event) {
        return -EINVAL;
    }
    if (g_udp_event_head == g_udp_event_tail) {
        return -EAGAIN;
    }

    idx = g_udp_event_tail;
    while (idx != g_udp_event_head) {
        struct net_udp_event *ev = &g_udp_events[idx];
        if (local_port == 0 || ev->dst_port == local_port) {
            uint32_t cur = idx;
            uint32_t next = (cur + 1U) % NET_UDP_EVENT_RING;
            *out_event = *ev;
            while (next != g_udp_event_head) {
                g_udp_events[cur] = g_udp_events[next];
                cur = next;
                next = (next + 1U) % NET_UDP_EVENT_RING;
            }
            g_udp_event_head = (g_udp_event_head + NET_UDP_EVENT_RING - 1U) % NET_UDP_EVENT_RING;
            return 0;
        }
        idx = (idx + 1U) % NET_UDP_EVENT_RING;
    }
    return -EAGAIN;
}

static int send_ipv4_udp(const uint8_t dst_mac[6], const uint8_t src_ip[4], const uint8_t dst_ip[4],
                         uint16_t src_port, uint16_t dst_port, const void *payload, size_t payload_len) {
    uint8_t frame[1514];
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    struct ipv4_hdr *ip = (struct ipv4_hdr *)(frame + sizeof(*eth));
    struct udp_hdr *udp = (struct udp_hdr *)(frame + sizeof(*eth) + sizeof(*ip));
    uint8_t *body = frame + sizeof(*eth) + sizeof(*ip) + sizeof(*udp);
    size_t ip_len = sizeof(*ip) + sizeof(*udp) + payload_len;
    size_t frame_len = sizeof(*eth) + ip_len;

    if (!payload || payload_len == 0 || frame_len > sizeof(frame)) {
        return -EINVAL;
    }

    mac_copy(eth->dst, dst_mac);
    mac_copy(eth->src, g_dev.mac);
    eth->ethertype = htons(ETH_TYPE_IPV4);

    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons((uint16_t)ip_len);
    ip->id = 0;
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IPV4_PROTO_UDP;
    ip->checksum = 0;
    memcpy(ip->saddr, src_ip, 4);
    memcpy(ip->daddr, dst_ip, 4);
    ip->checksum = ip_checksum(ip, sizeof(*ip));

    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->len = htons((uint16_t)(sizeof(*udp) + payload_len));
    udp->checksum = 0; /* Optional in IPv4, skipped for simplicity. */
    memcpy(body, payload, payload_len);

    return net_tx_raw(frame, frame_len);
}

static int send_ipv4_icmp(const uint8_t dst_mac[6], const uint8_t src_ip[4], const uint8_t dst_ip[4],
                          const void *icmp, size_t icmp_len) {
    uint8_t frame[1514];
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    struct ipv4_hdr *ip = (struct ipv4_hdr *)(frame + sizeof(*eth));
    uint8_t *body = frame + sizeof(*eth) + sizeof(*ip);
    size_t ip_len = sizeof(*ip) + icmp_len;
    size_t frame_len = sizeof(*eth) + ip_len;

    if (!icmp || icmp_len == 0 || frame_len > sizeof(frame)) {
        return -EINVAL;
    }

    mac_copy(eth->dst, dst_mac);
    mac_copy(eth->src, g_dev.mac);
    eth->ethertype = htons(ETH_TYPE_IPV4);

    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons((uint16_t)ip_len);
    ip->id = 0;
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IPV4_PROTO_ICMP;
    ip->checksum = 0;
    memcpy(ip->saddr, src_ip, 4);
    memcpy(ip->daddr, dst_ip, 4);
    ip->checksum = ip_checksum(ip, sizeof(*ip));

    memcpy(body, icmp, icmp_len);
    return net_tx_raw(frame, frame_len);
}

static int resolve_l2_next_hop(const uint8_t dst_ip[4], uint8_t out_mac[6]) {
    uint8_t nh_ip[4];
    if (!dst_ip || !out_mac) {
        return -EINVAL;
    }

    if (ip_in_same_subnet(g_dev.ip, dst_ip, g_dev.mask)) {
        memcpy(nh_ip, dst_ip, 4);
    } else {
        if (g_dev.gw[0] == 0 && g_dev.gw[1] == 0 && g_dev.gw[2] == 0 && g_dev.gw[3] == 0) {
            return -EHOSTUNREACH;
        }
        memcpy(nh_ip, g_dev.gw, 4);
    }

    if (g_arp.valid && ip_equal(g_arp.ip, nh_ip)) {
        memcpy(out_mac, g_arp.mac, 6);
        return 0;
    }
    (void)send_arp_request(nh_ip);
    return -EAGAIN;
}

int net_send_icmp_echo(const uint8_t dst_ip[4], const void *icmp_packet, size_t icmp_len) {
    uint8_t dst_mac[6];
    int ret;
    if (!g_dev.present || !dst_ip || !icmp_packet || icmp_len == 0) {
        return -EINVAL;
    }
    if ((g_dhcp.state != DHCP_BOUND) &&
        !(g_dev.ip[0] || g_dev.ip[1] || g_dev.ip[2] || g_dev.ip[3])) {
        return -ENETDOWN;
    }
    ret = resolve_l2_next_hop(dst_ip, dst_mac);
    if (ret < 0) {
        return ret;
    }
    return send_ipv4_icmp(dst_mac, g_dev.ip, dst_ip, icmp_packet, icmp_len);
}

int net_send_udp(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port,
                 const void *payload, size_t payload_len) {
    uint8_t dst_mac[6];
    int ret;
    if (!g_dev.present || !dst_ip || !payload || payload_len == 0) {
        return -EINVAL;
    }
    if (src_port == 0 || dst_port == 0) {
        return -EINVAL;
    }
    if ((g_dhcp.state != DHCP_BOUND) &&
        !(g_dev.ip[0] || g_dev.ip[1] || g_dev.ip[2] || g_dev.ip[3])) {
        return -ENETDOWN;
    }
    ret = resolve_l2_next_hop(dst_ip, dst_mac);
    if (ret < 0) {
        return ret;
    }
    return send_ipv4_udp(dst_mac, g_dev.ip, dst_ip, src_port, dst_port, payload, payload_len);
}

static void arp_cache_update(const uint8_t ip[4], const uint8_t mac[6]) {
    g_arp.valid = true;
    memcpy(g_arp.ip, ip, 4);
    memcpy(g_arp.mac, mac, 6);
}

static int send_arp_reply(const struct arp_hdr *req, const uint8_t dst_mac[6]) {
    uint8_t frame[sizeof(struct eth_hdr) + sizeof(struct arp_hdr)];
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    struct arp_hdr *arp = (struct arp_hdr *)(frame + sizeof(*eth));

    mac_copy(eth->dst, dst_mac);
    mac_copy(eth->src, g_dev.mac);
    eth->ethertype = htons(ETH_TYPE_ARP);

    arp->htype = htons(ARP_HTYPE_ETHERNET);
    arp->ptype = htons(ARP_PTYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(ARP_OP_REPLY);
    mac_copy(arp->sha, g_dev.mac);
    memcpy(arp->spa, g_dev.ip, 4);
    mac_copy(arp->tha, req->sha);
    memcpy(arp->tpa, req->spa, 4);

    return net_tx_raw(frame, sizeof(frame));
}

static int send_arp_request(const uint8_t target_ip[4]) {
    static const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t frame[sizeof(struct eth_hdr) + sizeof(struct arp_hdr)];
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    struct arp_hdr *arp = (struct arp_hdr *)(frame + sizeof(*eth));

    mac_copy(eth->dst, broadcast_mac);
    mac_copy(eth->src, g_dev.mac);
    eth->ethertype = htons(ETH_TYPE_ARP);

    arp->htype = htons(ARP_HTYPE_ETHERNET);
    arp->ptype = htons(ARP_PTYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(ARP_OP_REQUEST);
    mac_copy(arp->sha, g_dev.mac);
    memcpy(arp->spa, g_dev.ip, 4);
    memset(arp->tha, 0, 6);
    memcpy(arp->tpa, target_ip, 4);

    return net_tx_raw(frame, sizeof(frame));
}

static void dhcp_apply_fallback_static(void) {
    g_dev.ip[0] = 10; g_dev.ip[1] = 0; g_dev.ip[2] = 2; g_dev.ip[3] = 15;
    g_dev.mask[0] = 255; g_dev.mask[1] = 255; g_dev.mask[2] = 255; g_dev.mask[3] = 0;
    g_dev.gw[0] = 10; g_dev.gw[1] = 0; g_dev.gw[2] = 2; g_dev.gw[3] = 2;
    printk(KERN_WARNING "net: DHCP failed, falling back to static %u.%u.%u.%u\n",
           g_dev.ip[0], g_dev.ip[1], g_dev.ip[2], g_dev.ip[3]);
    (void)send_arp_request(g_dev.gw);
}

static int dhcp_send_discover(void) {
    static const uint8_t broadcast_ip[4] = {255, 255, 255, 255};
    static const uint8_t zero_ip[4] = {0, 0, 0, 0};
    static const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    struct dhcp_packet pkt;
    size_t opt = 0;
    uint8_t param_req[] = {DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_SERVER_ID};

    memset(&pkt, 0, sizeof(pkt));
    pkt.op = DHCP_OP_BOOTREQUEST;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = htonl(g_dhcp.xid);
    pkt.flags = htons(0x8000); /* Broadcast reply. */
    memcpy(pkt.chaddr, g_dev.mac, 6);
    pkt.magic = htonl(DHCP_MAGIC_COOKIE);
    pkt.options[opt++] = DHCP_OPT_MSG_TYPE;
    pkt.options[opt++] = 1;
    pkt.options[opt++] = DHCP_MSG_DISCOVER;
    pkt.options[opt++] = DHCP_OPT_PARAM_REQ;
    pkt.options[opt++] = (uint8_t)sizeof(param_req);
    memcpy(&pkt.options[opt], param_req, sizeof(param_req));
    opt += sizeof(param_req);
    pkt.options[opt++] = DHCP_OPT_END;

    g_dhcp.last_event_tick = (uint32_t)g_tick_count;
    g_dhcp.retries++;
    g_dhcp.state = DHCP_DISCOVER_SENT;
    g_dev.dhcp_events++;
    return send_ipv4_udp(broadcast_mac, zero_ip, broadcast_ip,
                         UDP_PORT_DHCP_CLIENT, UDP_PORT_DHCP_SERVER,
                         &pkt, sizeof(pkt));
}

static int dhcp_send_request(void) {
    static const uint8_t broadcast_ip[4] = {255, 255, 255, 255};
    static const uint8_t zero_ip[4] = {0, 0, 0, 0};
    static const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    struct dhcp_packet pkt;
    size_t opt = 0;

    memset(&pkt, 0, sizeof(pkt));
    pkt.op = DHCP_OP_BOOTREQUEST;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = htonl(g_dhcp.xid);
    pkt.flags = htons(0x8000);
    memcpy(pkt.chaddr, g_dev.mac, 6);
    pkt.magic = htonl(DHCP_MAGIC_COOKIE);
    pkt.options[opt++] = DHCP_OPT_MSG_TYPE;
    pkt.options[opt++] = 1;
    pkt.options[opt++] = DHCP_MSG_REQUEST;
    pkt.options[opt++] = DHCP_OPT_REQ_IP;
    pkt.options[opt++] = 4;
    memcpy(&pkt.options[opt], g_dhcp.offered_ip, 4);
    opt += 4;
    pkt.options[opt++] = DHCP_OPT_SERVER_ID;
    pkt.options[opt++] = 4;
    memcpy(&pkt.options[opt], g_dhcp.server_id, 4);
    opt += 4;
    pkt.options[opt++] = DHCP_OPT_END;

    g_dhcp.last_event_tick = (uint32_t)g_tick_count;
    g_dhcp.state = DHCP_REQUEST_SENT;
    g_dev.dhcp_events++;
    return send_ipv4_udp(broadcast_mac, zero_ip, broadcast_ip,
                         UDP_PORT_DHCP_CLIENT, UDP_PORT_DHCP_SERVER,
                         &pkt, sizeof(pkt));
}

static bool dhcp_parse_options(const uint8_t *opts, size_t opts_len,
                               uint8_t *msg_type, uint8_t mask[4], uint8_t router[4], uint8_t server_id[4]) {
    size_t i = 0;
    bool have_type = false;
    while (i < opts_len) {
        uint8_t code = opts[i++];
        if (code == DHCP_OPT_END) {
            break;
        }
        if (code == 0) {
            continue;
        }
        if (i >= opts_len) {
            break;
        }
        uint8_t len = opts[i++];
        if ((size_t)i + len > opts_len) {
            break;
        }
        if (code == DHCP_OPT_MSG_TYPE && len == 1) {
            *msg_type = opts[i];
            have_type = true;
        } else if (code == DHCP_OPT_SUBNET_MASK && len == 4) {
            memcpy(mask, &opts[i], 4);
        } else if (code == DHCP_OPT_ROUTER && len >= 4) {
            memcpy(router, &opts[i], 4);
        } else if (code == DHCP_OPT_SERVER_ID && len == 4) {
            memcpy(server_id, &opts[i], 4);
        }
        i += len;
    }
    return have_type;
}

static void dhcp_on_offer(const struct dhcp_packet *pkt) {
    uint8_t msg_type = 0;
    uint8_t mask[4] = {0, 0, 0, 0};
    uint8_t router[4] = {0, 0, 0, 0};
    uint8_t server[4] = {0, 0, 0, 0};
    if (!dhcp_parse_options(pkt->options, sizeof(pkt->options), &msg_type, mask, router, server)) {
        return;
    }
    if (msg_type != DHCP_MSG_OFFER || g_dhcp.state != DHCP_DISCOVER_SENT) {
        return;
    }
    memcpy(g_dhcp.offered_ip, &pkt->yiaddr, 4);
    memcpy(g_dhcp.offered_mask, mask, 4);
    memcpy(g_dhcp.offered_router, router, 4);
    memcpy(g_dhcp.server_id, server, 4);
    printk(KERN_INFO "net: DHCP offer %u.%u.%u.%u\n",
           g_dhcp.offered_ip[0], g_dhcp.offered_ip[1], g_dhcp.offered_ip[2], g_dhcp.offered_ip[3]);
    (void)dhcp_send_request();
}

static void dhcp_on_ack(const struct dhcp_packet *pkt) {
    uint8_t msg_type = 0;
    uint8_t mask[4] = {255, 255, 255, 0};
    uint8_t router[4] = {0, 0, 0, 0};
    uint8_t server[4] = {0, 0, 0, 0};
    (void)server;
    if (!dhcp_parse_options(pkt->options, sizeof(pkt->options), &msg_type, mask, router, server)) {
        return;
    }
    if (msg_type != DHCP_MSG_ACK || g_dhcp.state != DHCP_REQUEST_SENT) {
        return;
    }
    memcpy(g_dev.ip, &pkt->yiaddr, 4);
    memcpy(g_dev.mask, mask, 4);
    memcpy(g_dev.gw, router, 4);
    g_dhcp.state = DHCP_BOUND;
    g_dev.dhcp_events++;
    printk(KERN_INFO "net: DHCP bound ip=%u.%u.%u.%u gw=%u.%u.%u.%u\n",
           g_dev.ip[0], g_dev.ip[1], g_dev.ip[2], g_dev.ip[3],
           g_dev.gw[0], g_dev.gw[1], g_dev.gw[2], g_dev.gw[3]);
    if (!(g_dev.gw[0] == 0 && g_dev.gw[1] == 0 && g_dev.gw[2] == 0 && g_dev.gw[3] == 0)) {
        (void)send_arp_request(g_dev.gw);
    }
}

static void dhcp_tick(void) {
    if (!g_dev.present || g_dhcp.state == DHCP_BOUND || g_dhcp.state == DHCP_FAILED) {
        return;
    }
    if (g_dhcp.state == DHCP_DISABLED) {
        (void)dhcp_send_discover();
        return;
    }
    if ((uint32_t)g_tick_count - g_dhcp.last_event_tick < DHCP_RETRY_TICKS) {
        return;
    }
    if (g_dhcp.retries >= DHCP_MAX_RETRIES) {
        g_dhcp.state = DHCP_FAILED;
        dhcp_apply_fallback_static();
        return;
    }
    if (g_dhcp.state == DHCP_DISCOVER_SENT) {
        (void)dhcp_send_discover();
    } else if (g_dhcp.state == DHCP_REQUEST_SENT) {
        (void)dhcp_send_request();
    }
}

static int send_icmp_echo_reply(const struct eth_hdr *rx_eth,
                                const struct ipv4_hdr *rx_ip,
                                const uint8_t *icmp, size_t icmp_len) {
    uint8_t frame[1514];
    struct eth_hdr *eth = (struct eth_hdr *)frame;
    struct ipv4_hdr *ip = (struct ipv4_hdr *)(frame + sizeof(*eth));
    uint8_t *icmp_out = frame + sizeof(*eth) + sizeof(*ip);
    size_t total_ip_len;
    size_t total_frame_len;

    if (icmp_len < sizeof(struct icmp_echo_hdr)) {
        return -EINVAL;
    }
    if (sizeof(*eth) + sizeof(*ip) + icmp_len > sizeof(frame)) {
        return -EMSGSIZE;
    }

    mac_copy(eth->dst, rx_eth->src);
    mac_copy(eth->src, g_dev.mac);
    eth->ethertype = htons(ETH_TYPE_IPV4);

    ip->version_ihl = 0x45;
    ip->tos = 0;
    total_ip_len = sizeof(*ip) + icmp_len;
    ip->total_len = htons((uint16_t)total_ip_len);
    ip->id = 0;
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IPV4_PROTO_ICMP;
    ip->checksum = 0;
    memcpy(ip->saddr, g_dev.ip, 4);
    memcpy(ip->daddr, rx_ip->saddr, 4);
    ip->checksum = ip_checksum(ip, sizeof(*ip));

    memcpy(icmp_out, icmp, icmp_len);
    ((struct icmp_echo_hdr *)icmp_out)->type = ICMP_ECHO_REPLY;
    ((struct icmp_echo_hdr *)icmp_out)->code = 0;
    ((struct icmp_echo_hdr *)icmp_out)->checksum = 0;
    ((struct icmp_echo_hdr *)icmp_out)->checksum = ip_checksum(icmp_out, icmp_len);

    total_frame_len = sizeof(*eth) + total_ip_len;
    return net_tx_raw(frame, total_frame_len);
}

int net_init(void) {
    memset(&g_dev, 0, sizeof(g_dev));
    memset(&g_arp, 0, sizeof(g_arp));
    memset(&g_dhcp, 0, sizeof(g_dhcp));
    memset(g_icmp_events, 0, sizeof(g_icmp_events));
    g_icmp_event_head = 0;
    g_icmp_event_tail = 0;
    memset(g_udp_events, 0, sizeof(g_udp_events));
    g_udp_event_head = 0;
    g_udp_event_tail = 0;
    g_tick_count = 0;
    printk(KERN_INFO "net: core initialized\n");
    return 0;
}

int net_register_device(const char *name, const uint8_t mac[6],
                        const struct net_device_ops *ops, void *ctx) {
    if (!name || !mac || !ops || !ops->tx) {
        return -EINVAL;
    }
    if (g_dev.present) {
        return -EBUSY;
    }

    memset(&g_dev, 0, sizeof(g_dev));
    g_dev.present = true;
    strncpy(g_dev.name, name, sizeof(g_dev.name) - 1);
    memcpy(g_dev.mac, mac, 6);
    g_dev.ops = ops;
    g_dev.ctx = ctx;

    memset(g_dev.ip, 0, sizeof(g_dev.ip));
    memset(g_dev.mask, 0, sizeof(g_dev.mask));
    memset(g_dev.gw, 0, sizeof(g_dev.gw));
    g_dhcp.state = DHCP_DISABLED;
    g_dhcp.xid = 0x0B1E0000U ^ ((uint32_t)g_dev.mac[2] << 8) ^ g_dev.mac[5];
    g_dhcp.last_event_tick = 0;
    g_dhcp.retries = 0;

    printk(KERN_INFO "net: device %s registered mac=%02x:%02x:%02x:%02x:%02x:%02x (DHCP pending)\n",
           g_dev.name,
           g_dev.mac[0], g_dev.mac[1], g_dev.mac[2],
           g_dev.mac[3], g_dev.mac[4], g_dev.mac[5]);
    return 0;
}

void net_rx_frame(const uint8_t *frame, size_t len) {
    const struct eth_hdr *eth;
    uint16_t ethertype;

    if (!g_dev.present || !frame || len < sizeof(struct eth_hdr)) {
        return;
    }

    g_dev.rx_frames++;
    eth = (const struct eth_hdr *)frame;
    ethertype = ntohs(eth->ethertype);

    if (ethertype == ETH_TYPE_ARP) {
        const struct arp_hdr *arp;
        g_dev.arp_rx++;
        if (len < sizeof(struct eth_hdr) + sizeof(struct arp_hdr)) {
            g_dev.rx_drop++;
            return;
        }
        arp = (const struct arp_hdr *)(frame + sizeof(*eth));
        if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET ||
            ntohs(arp->ptype) != ARP_PTYPE_IPV4 ||
            arp->hlen != 6 || arp->plen != 4) {
            g_dev.rx_drop++;
            return;
        }

        arp_cache_update(arp->spa, arp->sha);
        if (ntohs(arp->oper) == ARP_OP_REQUEST && ip_equal(arp->tpa, g_dev.ip)) {
            (void)send_arp_reply(arp, eth->src);
        }
        return;
    }

    if (ethertype == ETH_TYPE_IPV4) {
        const struct ipv4_hdr *ip;
        const uint8_t *payload;
        const struct udp_hdr *udp;
        size_t ihl;
        size_t total_len;
        size_t payload_len;

        g_dev.ipv4_rx++;
        if (len < sizeof(struct eth_hdr) + sizeof(struct ipv4_hdr)) {
            g_dev.rx_drop++;
            return;
        }
        ip = (const struct ipv4_hdr *)(frame + sizeof(*eth));
        if ((ip->version_ihl >> 4) != 4) {
            g_dev.rx_drop++;
            return;
        }
        ihl = (size_t)(ip->version_ihl & 0x0F) * 4;
        if (ihl < sizeof(struct ipv4_hdr) ||
            len < sizeof(struct eth_hdr) + ihl) {
            g_dev.rx_drop++;
            return;
        }
        total_len = ntohs(ip->total_len);
        if (total_len < ihl || len < sizeof(struct eth_hdr) + total_len) {
            g_dev.rx_drop++;
            return;
        }
        payload = frame + sizeof(*eth) + ihl;
        payload_len = total_len - ihl;

        if (ip->protocol == IPV4_PROTO_UDP) {
            g_dev.udp_rx++;
            if (payload_len >= sizeof(struct udp_hdr)) {
                uint16_t src_port, dst_port, ulen;
                udp = (const struct udp_hdr *)payload;
                src_port = ntohs(udp->src_port);
                dst_port = ntohs(udp->dst_port);
                ulen = ntohs(udp->len);
                if (ulen >= sizeof(struct udp_hdr) && ulen <= payload_len &&
                    dst_port == UDP_PORT_DHCP_CLIENT && src_port == UDP_PORT_DHCP_SERVER &&
                    (g_dhcp.state == DHCP_DISCOVER_SENT || g_dhcp.state == DHCP_REQUEST_SENT)) {
                    const struct dhcp_packet *pkt =
                        (const struct dhcp_packet *)(payload + sizeof(struct udp_hdr));
                    size_t dlen = ulen - sizeof(struct udp_hdr);
                    if (dlen >= sizeof(struct dhcp_packet) &&
                        pkt->op == DHCP_OP_BOOTREPLY &&
                        ntohl(pkt->xid) == g_dhcp.xid &&
                        pkt->magic == htonl(DHCP_MAGIC_COOKIE)) {
                        dhcp_on_offer(pkt);
                        dhcp_on_ack(pkt);
                    }
                    return;
                }
                if (ip_equal(ip->daddr, g_dev.ip) && ulen > sizeof(struct udp_hdr)) {
                    const uint8_t *udp_payload = payload + sizeof(struct udp_hdr);
                    size_t udp_payload_len = ulen - sizeof(struct udp_hdr);
                    udp_event_push(ip->saddr, src_port, dst_port, udp_payload, udp_payload_len);
                }
            }
            return;
        }

        if (ip->protocol == IPV4_PROTO_ICMP) {
            if (!ip_equal(ip->daddr, g_dev.ip)) {
                return;
            }
            if (payload_len < sizeof(struct icmp_echo_hdr)) {
                g_dev.rx_drop++;
                return;
            }
            if (((const struct icmp_echo_hdr *)payload)->type == ICMP_ECHO_REQUEST) {
                g_dev.icmp_echo_rx++;
                (void)send_icmp_echo_reply(eth, ip, payload, payload_len);
            } else if (((const struct icmp_echo_hdr *)payload)->type == ICMP_ECHO_REPLY) {
                icmp_event_push(ip->saddr, payload, payload_len);
            }
            return;
        }
    }
}

void net_tick(void) {
    int ret;
    g_tick_count++;
    dhcp_tick();
    if (!g_dev.present || !g_dev.ops || !g_dev.ops->poll) {
        return;
    }
    /* Poll cadence to support environments where IRQ wiring is different. */
    if ((g_tick_count % 10) != 0) {
        return;
    }
    ret = g_dev.ops->poll(g_dev.ctx);
    if (ret < 0 && (g_tick_count % 200) == 0) {
        printk(KERN_WARNING "net: device poll returned %d\n", ret);
    }
}

bool net_is_ready(void) {
    return g_dev.present;
}
