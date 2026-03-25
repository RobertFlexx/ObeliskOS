/*
 * Obelisk OS - curl utility (minimal HTTP client)
 * From Axioms, Order.
 */

#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;

extern int printf(const char *fmt, ...);
extern void _exit(int status);
extern int socket(int domain, int type, int protocol);
extern int connect(int sockfd, const void *addr, int addrlen);
extern int bind(int sockfd, const void *addr, int addrlen);
extern int close(int fd);
extern int open(const char *pathname, int flags, int mode);
extern ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const void *dest_addr, int addrlen);
extern ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, void *src_addr, int *addrlen);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int strcmp(const char *a, const char *b);
extern size_t strlen(const char *s);

#define AF_INET 2
#define SOCK_DGRAM 2
#define SOCK_STREAM 1

#define O_WRONLY 0x0001
#define O_CREAT  0x0040
#define O_TRUNC  0x0200

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
} __attribute__((packed));

static uint16_t bswap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static uint16_t htons(uint16_t v) {
    return bswap16(v);
}

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
    out[oi++] = 0;
    return oi;
}

static int resolve_host_ipv4(const char *host, uint8_t out_ip[4]) {
    uint8_t dns_ip[4] = {10, 0, 2, 3};
    uint8_t pkt[512];
    uint8_t resp[512];
    struct sockaddr_in dst, src, bind_addr;
    int src_len = sizeof(src);
    int sock;
    size_t off = 0;
    ssize_t n;
    uint16_t qd, an;
    size_t ri;

    if (parse_ipv4(host, out_ip) == 0) return 0;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(53054);
    bind_addr.sin_addr = 0;
    for (int i = 0; i < 8; i++) bind_addr.sin_zero[i] = 0;
    (void)bind(sock, &bind_addr, sizeof(bind_addr));

    pkt[off++] = 0x22; pkt[off++] = 0x33;
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

static int parse_u16(const char *s, uint16_t *out) {
    uint32_t v = 0;
    if (!s || !*s || !out) return -1;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        v = (v * 10U) + (uint32_t)(*s - '0');
        if (v > 65535U) return -1;
        s++;
    }
    *out = (uint16_t)v;
    return 0;
}

static int parse_http_url(const char *url, char *host, size_t host_cap,
                          char *path, size_t path_cap, uint16_t *port_out) {
    const char *p;
    const char *host_start;
    const char *host_end;
    const char *path_start;
    size_t host_len;
    size_t path_len;
    *port_out = 80;
    if (!url || !host || !path || !port_out) return -1;
    if (url[0] == '\0') return -1;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
        url[4] == ':' && url[5] == '/' && url[6] == '/') {
        p = url + 7;
    } else if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
               url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/') {
        return -2;
    } else {
        return -1;
    }

    host_start = p;
    while (*p && *p != '/' && *p != ':') p++;
    host_end = p;
    if (host_end == host_start) return -1;

    if (*p == ':') {
        char pbuf[8];
        size_t pn = 0;
        p++;
        while (*p && *p != '/' && pn + 1 < sizeof(pbuf)) {
            pbuf[pn++] = *p++;
        }
        pbuf[pn] = '\0';
        if (parse_u16(pbuf, port_out) < 0) return -1;
    }

    path_start = (*p == '/') ? p : "/";
    host_len = (size_t)(host_end - host_start);
    path_len = strlen(path_start);
    if (host_len + 1 > host_cap || path_len + 1 > path_cap) return -1;
    for (size_t i = 0; i < host_len; i++) host[i] = host_start[i];
    host[host_len] = '\0';
    for (size_t i = 0; i <= path_len; i++) path[i] = path_start[i];
    return 0;
}

static void usage(void) {
    printf("Usage: curl [-o output_file] <http://url>\n");
}

static void print_version(void) {
    printf("curl 0.1 (Obelisk)\n");
    printf("Protocols: http\n");
    printf("Features: none\n");
}

static int write_all(int fd, const uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static int http_get_to_fd(const char *url, int out_fd) {
    char host[128];
    char path[256];
    uint16_t port;
    uint8_t ip[4];
    struct sockaddr_in sa;
    int sock;
    char req[512];
    size_t req_len = 0;
    uint8_t buf[1024];
    ssize_t n;
    int hdr_done = 0;
    int match = 0;

    int pr = parse_http_url(url, host, sizeof(host), path, sizeof(path), &port);
    if (pr == -2) {
        printf("curl: https is not supported yet (use http)\n");
        return 1;
    }
    if (pr < 0) {
        printf("curl: invalid url\n");
        return 1;
    }
    if (resolve_host_ipv4(host, ip) < 0) {
        printf("curl: cannot resolve host: %s\n", host);
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("curl: cannot open tcp socket (%d)\n", sock);
        return 1;
    }

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
                  ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
    for (int i = 0; i < 8; i++) sa.sin_zero[i] = 0;

    if (connect(sock, &sa, sizeof(sa)) < 0) {
        printf("curl: connect failed\n");
        close(sock);
        return 1;
    }

    {
        const char *pfx = "GET ";
        const char *mid = " HTTP/1.0\r\nHost: ";
        const char *ua  = "\r\nUser-Agent: obelisk-curl/0.1\r\nConnection: close\r\n\r\n";
        size_t pfx_len = strlen(pfx), path_len = strlen(path), mid_len = strlen(mid);
        size_t host_len = strlen(host), ua_len = strlen(ua);
        if (pfx_len + path_len + mid_len + host_len + ua_len + 1 > sizeof(req)) {
            printf("curl: request too long\n");
            close(sock);
            return 1;
        }
        for (size_t i = 0; i < pfx_len; i++) req[req_len++] = pfx[i];
        for (size_t i = 0; i < path_len; i++) req[req_len++] = path[i];
        for (size_t i = 0; i < mid_len; i++) req[req_len++] = mid[i];
        for (size_t i = 0; i < host_len; i++) req[req_len++] = host[i];
        for (size_t i = 0; i < ua_len; i++) req[req_len++] = ua[i];
    }

    if (sendto(sock, req, req_len, 0, NULL, 0) < 0) {
        printf("curl: send failed\n");
        close(sock);
        return 1;
    }

    while ((n = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL)) > 0) {
        size_t i = 0;
        if (!hdr_done) {
            while (i < (size_t)n) {
                uint8_t c = buf[i++];
                if (match == 0 && c == '\r') match = 1;
                else if (match == 1 && c == '\n') match = 2;
                else if (match == 2 && c == '\r') match = 3;
                else if (match == 3 && c == '\n') {
                    hdr_done = 1;
                    break;
                } else match = 0;
            }
        }
        if (hdr_done && i < (size_t)n) {
            if (write_all(out_fd, buf + i, (size_t)n - i) < 0) {
                close(sock);
                return 1;
            }
        }
    }

    close(sock);
    if (!hdr_done) {
        printf("curl: invalid http response\n");
        return 1;
    }
    return 0;
}

static __attribute__((used)) void curl_main(int argc, char **argv) {
    const char *url = NULL;
    const char *outfile = NULL;
    int out_fd = 1;
    int rc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            _exit(0);
        }
        if (strcmp(argv[i], "--cacert") == 0 || strcmp(argv[i], "--proto") == 0) {
            if (i + 1 >= argc) {
                usage();
                _exit(1);
            }
            i++;
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0' &&
            strcmp(argv[i], "-o") != 0) {
            /* Compatibility flags used by opkg (-f, -s, -S, -L, combos). */
            const char *p = argv[i] + 1;
            int ok = 1;
            while (*p) {
                if (*p != 'f' && *p != 's' && *p != 'S' && *p != 'L') {
                    ok = 0;
                    break;
                }
                p++;
            }
            if (ok) {
                continue;
            }
            usage();
            _exit(1);
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage();
                _exit(1);
            }
            outfile = argv[++i];
            continue;
        }
        url = argv[i];
    }

    if (!url) {
        usage();
        _exit(1);
    }

    if (outfile) {
        out_fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            printf("curl: cannot open output: %s\n", outfile);
            _exit(1);
        }
    }

    rc = http_get_to_fd(url, out_fd);
    if (outfile) {
        close(out_fd);
    }
    _exit(rc);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "andq $-16, %rsp\n"
        "call curl_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}
