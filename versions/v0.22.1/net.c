#include "net.h"
#include "virtio_net.h"
#include "kstring.h"
#include "console.h"
#include "time.h"

#define OUR_IP  0x0A00020FU        /* 10.0.2.15 */
#define GW_IP   0x0A000202U        /* 10.0.2.2  */
#define DNS_IP  0x0A000203U        /* 10.0.2.3  */
#define NETMASK 0xFFFFFF00U

#define ETH_ARP 0x0806
#define ETH_IP  0x0800

static unsigned char our_mac[6];
static int ready;
static int have_gw;

/* ---- 工具 ---- */

static void put16(unsigned char *p, unsigned int v) { p[0] = (unsigned char) (v >> 8); p[1] = (unsigned char) v; }
static void put32(unsigned char *p, unsigned int v) { p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16); p[2] = (unsigned char) (v >> 8); p[3] = (unsigned char) v; }
static unsigned int get16(const unsigned char *p) { return ((unsigned int) p[0] << 8) | p[1]; }
static unsigned int get32(const unsigned char *p) { return ((unsigned int) p[0] << 24) | ((unsigned int) p[1] << 16) | ((unsigned int) p[2] << 8) | p[3]; }

static unsigned int checksum(const unsigned char *data, unsigned int len) {
    unsigned int sum = 0;
    unsigned int i;
    for (i = 0; i + 1 < len; i += 2) sum += get16(data + i);
    if (len & 1) sum += (unsigned int) data[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (~sum) & 0xFFFF;
}

static void mac_copy(unsigned char *dst, const unsigned char *src) { int i; for (i = 0; i < 6; i++) dst[i] = src[i]; }

/* ---- ARP ---- */

struct arp_entry { unsigned int ip; unsigned char mac[6]; int valid; };
static struct arp_entry arp_cache[8];

static void arp_learn(unsigned int ip, const unsigned char *mac) {
    int i, free_slot = -1;
    for (i = 0; i < 8; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) { mac_copy(arp_cache[i].mac, mac); return; }
        if (!arp_cache[i].valid && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        arp_cache[free_slot].ip = ip;
        mac_copy(arp_cache[free_slot].mac, mac);
        arp_cache[free_slot].valid = 1;
    }
}

static int arp_lookup(unsigned int ip, unsigned char *mac) {
    int i;
    for (i = 0; i < 8; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) { if (mac) mac_copy(mac, arp_cache[i].mac); return 1; }
    }
    return 0;
}

static void arp_send(unsigned int target, int request, const unsigned char *dst_mac) {
    unsigned char frame[42];
    unsigned char *a = frame + 14;
    memset(frame, 0, sizeof(frame));
    if (request) { memset(frame, 0xFF, 6); }
    else mac_copy(frame, dst_mac);
    mac_copy(frame + 6, our_mac);
    put16(frame + 12, ETH_ARP);

    put16(a + 0, 1);           /* htype */
    put16(a + 2, ETH_IP);      /* ptype */
    a[4] = 6; a[5] = 4;
    put16(a + 6, request ? 1 : 2);
    mac_copy(a + 8, our_mac);
    put32(a + 14, OUR_IP);
    if (request) memset(a + 18, 0, 6); else mac_copy(a + 18, dst_mac);
    put32(a + 24, target);

    virtio_net_send(frame, sizeof(frame));
}

static void arp_handle(const unsigned char *a, unsigned int len) {
    unsigned int op, spa;
    if (len < 28) return;
    op = get16(a + 6);
    spa = get32(a + 14);
    arp_learn(spa, a + 8);
    if (op == 1 && get32(a + 24) == OUR_IP) {
        arp_send(spa, 0, a + 8);
    }
}

static int arp_resolve(unsigned int ip, unsigned char *mac) {
    unsigned int start;
    if (arp_lookup(ip, mac)) return 1;
    arp_send(ip, 1, 0);
    start = time_ms();
    while (time_ms() - start < 1000) {
        net_poll();
        if (arp_lookup(ip, mac)) return 1;
        __asm__ __volatile__("pause");
    }
    return 0;
}

/* ---- IP / ICMP / UDP ---- */

static volatile int got_icmp;
static volatile unsigned int icmp_id;
static volatile int got_udp;
static volatile unsigned int udp_sport, udp_dport;
static unsigned char udp_payload[1600];
static volatile unsigned int udp_len;

static int ip_send(unsigned int dst, unsigned char proto, const unsigned char *payload, unsigned int len) {
    unsigned char frame[1600];
    unsigned char *ip = frame + 14;
    unsigned int total = 20 + len;
    unsigned char dstmac[6];

    if (total > 1500) return -1;
    if (!arp_resolve(dst, dstmac)) return -1;

    mac_copy(frame, dstmac);
    mac_copy(frame + 6, our_mac);
    put16(frame + 12, ETH_IP);

    ip[0] = 0x45;
    ip[1] = 0;
    put16(ip + 2, total);
    put16(ip + 4, 0x1234);
    put16(ip + 6, 0);
    ip[8] = 64;
    ip[9] = proto;
    put16(ip + 10, 0);
    put32(ip + 12, OUR_IP);
    put32(ip + 16, dst);

    memcpy(ip + 20, payload, len);
    put16(ip + 10, checksum(ip, 20));

    return virtio_net_send(frame, 14 + total);
}

static void icmp_send_echo(unsigned int dst, unsigned int id, unsigned int seq) {
    unsigned char pkt[32];
    unsigned int i;
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 8; pkt[1] = 0;
    put16(pkt + 4, id);
    put16(pkt + 6, seq);
    for (i = 8; i < 24; i++) pkt[i] = (unsigned char) ('a' + i % 26);
    put16(pkt + 2, checksum(pkt, 24));
    ip_send(dst, 1, pkt, 24);
}

static void udp_send(unsigned int dst, unsigned int sport, unsigned int dport,
                     const unsigned char *data, unsigned int len) {
    unsigned char pkt[1600];
    put16(pkt + 0, sport);
    put16(pkt + 2, dport);
    put16(pkt + 4, 8 + len);
    put16(pkt + 6, 0);
    memcpy(pkt + 8, data, len);
    ip_send(dst, 17, pkt, 8 + len);
}

static void ip_handle(const unsigned char *ip, unsigned int len) {
    unsigned int ihl, total, proto;
    unsigned int src, dst;

    if (len < 20) return;
    if ((ip[0] >> 4) != 4) return;
    ihl = (unsigned int) (ip[0] & 0xF) * 4;
    if (ihl < 20 || ihl > len) return;
    total = get16(ip + 2);
    if (total > len) return;
    proto = ip[9];
    src = get32(ip + 12);
    dst = get32(ip + 16);

    if (dst != OUR_IP) return;

    if (proto == 1 && total >= ihl + 8) {
        const unsigned char *ic = ip + ihl;
        if (ic[0] == 0 && get16(ic + 4) == icmp_id) got_icmp = 1;
    } else if (proto == 17 && total >= ihl + 8) {
        const unsigned char *u = ip + ihl;
        unsigned int sp = get16(u), dp = get16(u + 2), ulen = get16(u + 4);
        if (ulen >= 8 && ulen <= total - ihl) {
            if (got_udp == 0 && sp == udp_dport) {
                memcpy(udp_payload, u + 8, ulen - 8);
                udp_len = ulen - 8;
                got_udp = 1;
                (void) dp; (void) src;
            }
        }
    }
}

void net_poll(void) {
    unsigned char frame[1600];
    unsigned int len;
    while (virtio_net_recv(frame, sizeof(frame), &len)) {
        unsigned int type;
        if (len < 14) continue;
        type = get16(frame + 12);
        if (type == ETH_ARP) arp_handle(frame + 14, len - 14);
        else if (type == ETH_IP) ip_handle(frame + 14, len - 14);
    }
}

/* ---- 对外 ---- */

void net_init(void) {
    int i;
    if (!virtio_net_init()) {
        ready = 0;
        return;
    }
    { const unsigned char *m = virtio_net_mac(); for (i = 0; i < 6; i++) our_mac[i] = m[i]; }
    memset(arp_cache, 0, sizeof(arp_cache));
    ready = 1;
    have_gw = arp_resolve(GW_IP, 0);
}

int net_ready(void) { return ready; }

void net_info(void) {
    if (!ready) { console_write("net: no virtio-net device\n"); return; }
    console_write("mac: ");
    { int i; for (i = 0; i < 6; i++) { console_write_hex(our_mac[i]); if (i < 5) console_write(":"); } }
    console_write("\nip:  10.0.2.15/24  gw: 10.0.2.2  dns: 10.0.2.3\n");
    console_write("link: ");
    console_write(have_gw ? "up (gateway resolved)\n" : "up (gateway not resolved)\n");
}

int net_ping(unsigned int ip, unsigned int *rtt_ms) {
    unsigned int start;
    int tries;
    if (!ready) return 0;
    for (tries = 0; tries < 3; tries++) {
        got_icmp = 0;
        icmp_id = (unsigned int) (0x1000 + tries);
        start = time_ms();
        icmp_send_echo(ip, icmp_id, 1);
        while (time_ms() - start < 1000) {
            net_poll();
            if (got_icmp) { *rtt_ms = time_ms() - start; return 1; }
            __asm__ __volatile__("sti; hlt");
        }
    }
    return 0;
}

static unsigned int dns_query_id;

static int dns_build(const char *name, unsigned char *buf, unsigned int cap) {
    unsigned int n = 0, i = 0, len;
    if (cap < 12 + 256) return -1;
    dns_query_id = (unsigned int) (time_ms() & 0xFFFF);
    put16(buf + 0, dns_query_id);
    put16(buf + 2, 0x0100);      /* standard query, recursion desired */
    put16(buf + 4, 1);           /* qdcount */
    put16(buf + 6, 0);
    put16(buf + 8, 0);
    put16(buf + 10, 0);
    n = 12;
    while (name[i]) {
        unsigned int start = i;
        while (name[i] && name[i] != '.') i++;
        len = i - start;
        if (len == 0 || len > 63) return -1;
        buf[n++] = (unsigned char) len;
        memcpy(buf + n, name + start, len);
        n += len;
        if (name[i] == '.') i++;
    }
    buf[n++] = 0;
    put16(buf + n, 1); n += 2;   /* type A */
    put16(buf + n, 1); n += 2;   /* class IN */
    return (int) n;
}

int net_dns(const char *name, unsigned int *out_ip) {
    unsigned char query[300];
    unsigned int qlen, start;
    if (!ready) return 0;
    qlen = (unsigned int) dns_build(name, query, sizeof(query));
    if ((int) qlen < 0) return 0;

    got_udp = 0;
    udp_dport = 53;
    udp_sport = 53000;
    udp_send(DNS_IP, udp_sport, 53, query, qlen);

    start = time_ms();
    while (time_ms() - start < 2000) {
        net_poll();
        if (got_udp) {
            unsigned int n = 12, qd, an, k;
            if (udp_len < 12) return 0;
            qd = get16(udp_payload + 4);
            an = get16(udp_payload + 6);
            for (k = 0; k < qd; k++) {
                while (n < udp_len && udp_payload[n]) n += udp_payload[n] + 1;
                n += 1 + 4;
            }
            for (k = 0; k < an && n + 12 <= udp_len; k++) {
                if ((udp_payload[n] & 0xC0) == 0xC0) n += 2;
                else { while (n < udp_len && udp_payload[n]) n += udp_payload[n] + 1; n++; }
                if (n + 10 > udp_len) return 0;
                {
                    unsigned int type = get16(udp_payload + n);
                    unsigned int rdlen = get16(udp_payload + n + 8);
                    n += 10;
                    if (type == 1 && rdlen == 4 && n + 4 <= udp_len) {
                        *out_ip = get32(udp_payload + n);
                        return 1;
                    }
                    n += rdlen;
                }
            }
            return 0;
        }
        __asm__ __volatile__("sti; hlt");
    }
    return 0;
}

int net_parse_ip(const char *s, unsigned int *out) {
    unsigned int parts[4], n = 0, val = 0;
    int digits = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') { val = val * 10 + (unsigned int) (*s - '0'); digits = 1; }
        else if (*s == '.') { if (!digits || n >= 3) return 0; parts[n++] = val; val = 0; digits = 0; }
        else return 0;
        s++;
    }
    if (!digits || n != 3) return 0;
    parts[3] = val;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 1;
}
