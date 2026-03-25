/*
 * Obelisk OS - ping utility (minimal UNIX-style)
 * From Axioms, Order.
 */

#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;

extern int printf(const char *fmt, ...);
extern void _exit(int status);
extern int socket(int domain, int type, int protocol);
extern int bind(int sockfd, const void *addr, int addrlen);
extern int close(int fd);
extern ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const void *dest_addr, int addrlen);
extern ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, void *src_addr, int *addrlen);
extern int getpid(void);
extern int strcmp(const char *a, const char *b);
extern size_t strlen(const char *s);

#define AF_INET 2
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define IPPROTO_ICMP 1

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
} __attribute__((packed));

struct icmp_echo {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t seq;
    uint8_t payload[56];
} __attribute__((packed));

static int parse_ipv4(const char *s, uint8_t ip[4]);

static uint16_t bswap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static uint16_t htons(uint16_t v) {
    return bswap16(v);
}

static uint16_t checksum16(const void *buf, size_t len) {
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
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFU);
}

static int parse_uint(const char *s, int *out) {
    int v = 0;
    if (!s || !*s || !out) {
        return -1;
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        v = v * 10 + (*s - '0');
        s++;
    }
    *out = v;
    return 0;
}

static size_t dns_encode_name(uint8_t *out, size_t cap, const char *name) {
    size_t oi = 0;
    size_t i = 0;
    size_t label_start = 0;
    size_t nlen = strlen(name);
    if (!out || !name || cap < 2) return 0;
    while (i <= nlen) {
        if (name[i] == '.' || name[i] == '\0') {
            size_t llen = i - label_start;
            if (llen == 0 || llen > 63 || oi + 1 + llen >= cap) return 0;
            out[oi++] = (uint8_t)llen;
            for (size_t j = 0; j < llen; j++) out[oi++] = (uint8_t)name[label_start + j];
            label_start = i + 1;
        }
        i++;
    }
    if (oi >= cap) return 0;
    out[oi++] = 0;
    return oi;
}

static int resolve_host_ipv4(const char *host, uint8_t out_ip[4]) {
    uint8_t dns_ip[4] = {10, 0, 2, 3}; /* QEMU usernet DNS default */
    uint8_t pkt[512];
    uint8_t resp[512];
    struct sockaddr_in dst, src, bind_addr;
    int src_len = sizeof(src);
    int sock;
    size_t off = 0;
    ssize_t n;
    uint16_t qd, an;
    size_t ri;

    if (parse_ipv4(host, out_ip) == 0) {
        return 0;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(53053);
    bind_addr.sin_addr = 0;
    for (int i = 0; i < 8; i++) bind_addr.sin_zero[i] = 0;
    (void)bind(sock, &bind_addr, sizeof(bind_addr));

    pkt[off++] = 0x12; pkt[off++] = 0x34;
    pkt[off++] = 0x01; pkt[off++] = 0x00;
    pkt[off++] = 0x00; pkt[off++] = 0x01;
    pkt[off++] = 0x00; pkt[off++] = 0x00;
    pkt[off++] = 0x00; pkt[off++] = 0x00;
    pkt[off++] = 0x00; pkt[off++] = 0x00;
    {
        size_t w = dns_encode_name(pkt + off, sizeof(pkt) - off, host);
        if (w == 0) {
            close(sock);
            return -1;
        }
        off += w;
    }
    pkt[off++] = 0x00; pkt[off++] = 0x01;
    pkt[off++] = 0x00; pkt[off++] = 0x01;

    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    dst.sin_addr = ((uint32_t)dns_ip[0] << 24) | ((uint32_t)dns_ip[1] << 16) |
                   ((uint32_t)dns_ip[2] << 8) | (uint32_t)dns_ip[3];
    for (int i = 0; i < 8; i++) dst.sin_zero[i] = 0;

    n = sendto(sock, pkt, off, 0, &dst, sizeof(dst));
    if (n < 0) {
        close(sock);
        return -1;
    }

    n = recvfrom(sock, resp, sizeof(resp), 0, &src, &src_len);
    if (n < 12) {
        close(sock);
        return -1;
    }

    qd = (uint16_t)((resp[4] << 8) | resp[5]);
    an = (uint16_t)((resp[6] << 8) | resp[7]);
    ri = 12;
    for (uint16_t qi = 0; qi < qd; qi++) {
        while (ri < (size_t)n && resp[ri] != 0) ri += (size_t)resp[ri] + 1;
        ri++;
        ri += 4;
    }
    for (uint16_t ai = 0; ai < an; ai++) {
        uint16_t type, classv, rdlen;
        if (ri + 12 > (size_t)n) break;
        if ((resp[ri] & 0xC0) == 0xC0) ri += 2;
        else {
            while (ri < (size_t)n && resp[ri] != 0) ri += (size_t)resp[ri] + 1;
            ri++;
        }
        if (ri + 10 > (size_t)n) break;
        type = (uint16_t)((resp[ri] << 8) | resp[ri + 1]); ri += 2;
        classv = (uint16_t)((resp[ri] << 8) | resp[ri + 1]); ri += 2;
        ri += 4;
        rdlen = (uint16_t)((resp[ri] << 8) | resp[ri + 1]); ri += 2;
        if (ri + rdlen > (size_t)n) break;
        if (type == 1 && classv == 1 && rdlen == 4) {
            out_ip[0] = resp[ri];
            out_ip[1] = resp[ri + 1];
            out_ip[2] = resp[ri + 2];
            out_ip[3] = resp[ri + 3];
            close(sock);
            return 0;
        }
        ri += rdlen;
    }

    close(sock);
    return -1;
}

static int parse_ipv4(const char *s, uint8_t ip[4]) {
    int part = 0;
    int idx = 0;
    if (!s || !ip) {
        return -1;
    }
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            part = part * 10 + (*s - '0');
            if (part > 255) return -1;
        } else if (*s == '.') {
            if (idx >= 3) return -1;
            ip[idx++] = (uint8_t)part;
            part = 0;
        } else {
            return -1;
        }
        s++;
    }
    if (idx != 3) return -1;
    ip[3] = (uint8_t)part;
    return 0;
}

static void usage(void) {
    printf("Usage: ping [-c count] <host|ipv4-address>\n");
    printf("Examples:\n");
    printf("  ping 8.8.8.8\n");
    printf("  ping -c 4 1.1.1.1\n");
    printf("  ping google.com\n");
}

static __attribute__((used)) void ping_main(int argc, char **argv) {
    int sock;
    int count = 4;
    const char *target = NULL;
    uint8_t dst_ip[4];
    int ident = getpid() & 0xFFFF;
    int sent = 0;
    int recv_ok = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc || parse_uint(argv[++i], &count) < 0 || count <= 0) {
                usage();
                _exit(1);
            }
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] == 'c' && argv[i][2] != '\0') {
            if (parse_uint(argv[i] + 2, &count) < 0 || count <= 0) {
                usage();
                _exit(1);
            }
            continue;
        }
        target = argv[i];
    }

    if (!target) {
        usage();
        _exit(1);
    }
    if (resolve_host_ipv4(target, dst_ip) < 0) {
        printf("ping: cannot resolve %s\n", target);
        _exit(1);
    }

    sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        printf("ping: cannot open ICMP socket (%d)\n", sock);
        _exit(1);
    }

    printf("PING %s: %u data bytes\n", target, (unsigned)sizeof(((struct icmp_echo *)0)->payload));

    for (int seq = 1; seq <= count; seq++) {
        struct sockaddr_in to;
        struct sockaddr_in from;
        struct icmp_echo req;
        struct icmp_echo rep;
        int from_len = sizeof(from);
        ssize_t n;

        to.sin_family = AF_INET;
        to.sin_port = 0;
        to.sin_addr = ((uint32_t)dst_ip[0] << 24) |
                      ((uint32_t)dst_ip[1] << 16) |
                      ((uint32_t)dst_ip[2] << 8) |
                      (uint32_t)dst_ip[3];
        for (int j = 0; j < 8; j++) to.sin_zero[j] = 0;

        req.type = 8;
        req.code = 0;
        req.ident = htons((uint16_t)ident);
        req.seq = htons((uint16_t)seq);
        for (size_t j = 0; j < sizeof(req.payload); j++) {
            req.payload[j] = (uint8_t)j;
        }
        req.checksum = 0;
        req.checksum = htons(checksum16(&req, sizeof(req)));

        n = sendto(sock, &req, sizeof(req), 0, &to, sizeof(to));
        if (n < 0) {
            printf("ping: sendto failed (%d)\n", (int)n);
            continue;
        }
        sent++;

        n = recvfrom(sock, &rep, sizeof(rep), 0, &from, &from_len);
        if (n < 0) {
            printf("Request timeout for icmp_seq %d\n", seq);
            continue;
        }
        if ((size_t)n < sizeof(struct icmp_echo) - sizeof(rep.payload) || rep.type != 0) {
            printf("ping: received non-echo-reply packet\n");
            continue;
        }
        if (rep.ident != htons((uint16_t)ident)) {
            continue;
        }

        printf("%d bytes from %u.%u.%u.%u: icmp_seq=%d\n",
               (int)n,
               (unsigned)((from.sin_addr >> 24) & 0xFF),
               (unsigned)((from.sin_addr >> 16) & 0xFF),
               (unsigned)((from.sin_addr >> 8) & 0xFF),
               (unsigned)(from.sin_addr & 0xFF),
               seq);
        recv_ok++;
    }

    close(sock);
    printf("\n--- %s ping statistics ---\n", target);
    printf("%d packets transmitted, %d packets received\n", sent, recv_ok);
    _exit((recv_ok > 0) ? 0 : 1);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "andq $-16, %rsp\n"
        "call ping_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}
