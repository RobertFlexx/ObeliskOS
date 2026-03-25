/*
 * Obelisk OS - Minimal Network Core
 * From Axioms, Order.
 */

#ifndef _NET_NET_H
#define _NET_NET_H

#include <obelisk/types.h>

struct net_device_ops {
    int (*tx)(void *ctx, const void *frame, size_t len);
    int (*poll)(void *ctx);
};

struct net_icmp_event {
    uint8_t src_ip[4];
    size_t packet_len;
    uint8_t packet[256];
};

struct net_udp_event {
    uint8_t src_ip[4];
    uint16_t src_port;
    uint16_t dst_port;
    size_t payload_len;
    uint8_t payload[512];
};

int net_init(void);
int net_register_device(const char *name, const uint8_t mac[6],
                        const struct net_device_ops *ops, void *ctx);
void net_rx_frame(const uint8_t *frame, size_t len);
void net_tick(void);
bool net_is_ready(void);
int net_send_icmp_echo(const uint8_t dst_ip[4], const void *icmp_packet, size_t icmp_len);
int net_recv_icmp_echo_event(struct net_icmp_event *out_event);
int net_send_udp(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port,
                 const void *payload, size_t payload_len);
int net_recv_udp_event(uint16_t local_port, struct net_udp_event *out_event);
int net_tcp_connect(const uint8_t dst_ip[4], uint16_t dst_port, uint16_t local_port, int *out_conn_id);
int net_tcp_is_connected(int conn_id);
int net_tcp_send(int conn_id, const void *data, size_t len);
int net_tcp_recv(int conn_id, void *buf, size_t len, bool *peer_closed);
int net_tcp_close(int conn_id);

#endif /* _NET_NET_H */
