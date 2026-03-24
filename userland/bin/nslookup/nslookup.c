/*
 * Obelisk OS - nslookup utility (minimal A-record resolver)
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
extern int strcmp(const char *a, const char *b);
extern size_t strlen(const char *s);

#define AF_INET 2
#define SOCK_DGRAM 2

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
} __attribute__((packed));

static uint16_t bswap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint16_t htons(uint16_t v) { return bswap16(v); }

static int parse_ipv4(const char *s, uint8_t ip[4]) {
    int part = 0;
    int idx = 0;
    if (!s || !ip) return -1;
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

static void usage(void) {
    printf("Usage: nslookup <host> [dns-server-ip]\n");
}

static __attribute__((used)) void nslookup_main(int argc, char **argv) {
    const char *host;
    const char *dns_ip_str = "10.0.2.3";
    uint8_t dns_ip[4];
    uint8_t pkt[512];
    uint8_t resp[512];
    int sock;
    struct sockaddr_in dst, src, bind_addr;
    int src_len = sizeof(src);
    size_t off = 0;
    ssize_t n;
    uint16_t qd, an;
    size_t ri;

    if (argc < 2) {
        usage();
        _exit(1);
    }
    host = argv[1];
    if (argc >= 3) dns_ip_str = argv[2];
    if (parse_ipv4(dns_ip_str, dns_ip) < 0) {
        printf("nslookup: invalid DNS server IP\n");
        _exit(1);
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("nslookup: socket failed (%d)\n", sock);
        _exit(1);
    }

    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(53053);
    bind_addr.sin_addr = 0;
    for (int i = 0; i < 8; i++) bind_addr.sin_zero[i] = 0;
    (void)bind(sock, &bind_addr, sizeof(bind_addr));

    /* DNS header */
    pkt[off++] = 0x12; pkt[off++] = 0x34; /* id */
    pkt[off++] = 0x01; pkt[off++] = 0x00; /* recursion desired */
    pkt[off++] = 0x00; pkt[off++] = 0x01; /* qdcount */
    pkt[off++] = 0x00; pkt[off++] = 0x00; /* ancount */
    pkt[off++] = 0x00; pkt[off++] = 0x00; /* nscount */
    pkt[off++] = 0x00; pkt[off++] = 0x00; /* arcount */
    {
        size_t w = dns_encode_name(pkt + off, sizeof(pkt) - off, host);
        if (w == 0) {
            printf("nslookup: invalid host name\n");
            close(sock);
            _exit(1);
        }
        off += w;
    }
    pkt[off++] = 0x00; pkt[off++] = 0x01; /* type A */
    pkt[off++] = 0x00; pkt[off++] = 0x01; /* class IN */

    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    dst.sin_addr = ((uint32_t)dns_ip[0] << 24) | ((uint32_t)dns_ip[1] << 16) |
                   ((uint32_t)dns_ip[2] << 8) | (uint32_t)dns_ip[3];
    for (int i = 0; i < 8; i++) dst.sin_zero[i] = 0;

    n = sendto(sock, pkt, off, 0, &dst, sizeof(dst));
    if (n < 0) {
        printf("nslookup: send failed (%ld)\n", n);
        close(sock);
        _exit(1);
    }

    n = recvfrom(sock, resp, sizeof(resp), 0, &src, &src_len);
    if (n < 0 || n < 12) {
        printf("nslookup: receive failed (%ld)\n", n);
        close(sock);
        _exit(1);
    }

    qd = (uint16_t)((resp[4] << 8) | resp[5]);
    an = (uint16_t)((resp[6] << 8) | resp[7]);
    ri = 12;

    /* skip questions */
    for (uint16_t qi = 0; qi < qd; qi++) {
        while (ri < (size_t)n && resp[ri] != 0) {
            ri += (size_t)resp[ri] + 1;
        }
        ri++; /* zero */
        ri += 4; /* type+class */
    }

    for (uint16_t ai = 0; ai < an; ai++) {
        uint16_t type, classv, rdlen;
        if (ri + 12 > (size_t)n) break;
        if ((resp[ri] & 0xC0) == 0xC0) {
            ri += 2; /* compressed name */
        } else {
            while (ri < (size_t)n && resp[ri] != 0) ri += (size_t)resp[ri] + 1;
            ri++;
        }
        if (ri + 10 > (size_t)n) break;
        type = (uint16_t)((resp[ri] << 8) | resp[ri + 1]); ri += 2;
        classv = (uint16_t)((resp[ri] << 8) | resp[ri + 1]); ri += 2;
        ri += 4; /* ttl */
        rdlen = (uint16_t)((resp[ri] << 8) | resp[ri + 1]); ri += 2;
        if (ri + rdlen > (size_t)n) break;
        if (type == 1 && classv == 1 && rdlen == 4) {
            printf("Name: %s\n", host);
            printf("Address: %u.%u.%u.%u\n", (unsigned)resp[ri], (unsigned)resp[ri + 1],
                   (unsigned)resp[ri + 2], (unsigned)resp[ri + 3]);
            close(sock);
            _exit(0);
        }
        ri += rdlen;
    }

    printf("nslookup: no A record found for %s\n", host);
    close(sock);
    _exit(1);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "andq $-16, %rsp\n"
        "call nslookup_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}
