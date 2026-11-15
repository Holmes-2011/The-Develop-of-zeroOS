#include "virtio_net.h"
#include "pci.h"
#include "io.h"
#include "paging.h"
#include "kstring.h"
#include "serial.h"
#include "time.h"

/* 传统（legacy）virtio-net：I/O BAR0、两个 split virtqueue（0=RX，1=TX）。
   queue size 是设备只读的（QEMU 是 256），used ring 的偏移由它算出来。 */

#define RING_BYTES 16384
#define STATUS    0x12
#define QSELECT   0x0E
#define QSIZE_R   0x0C
#define QADDR     0x08
#define QNOTIFY   0x10
#define FEATURES  0x00
#define GUESTFEAT 0x04

struct vring_desc {
    unsigned long long addr;
    unsigned int  len;
    unsigned short flags;
    unsigned short next;
} __attribute__((packed));

struct vring_avail {
    unsigned short flags;
    unsigned short idx;
    unsigned short ring[];
} __attribute__((packed));

struct vring_used_elem {
    unsigned int id;
    unsigned int len;
} __attribute__((packed));

struct vring_used {
    unsigned short flags;
    unsigned short idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

static unsigned char rx_ring[RING_BYTES] __attribute__((aligned(4096)));
static unsigned char tx_ring[RING_BYTES] __attribute__((aligned(4096)));
static unsigned char rx_buf[64][2048] __attribute__((aligned(16)));
static unsigned char tx_buf[2048] __attribute__((aligned(16)));

static unsigned short io;
static int ready;
static unsigned char mac[6];
static unsigned int rx_last_used, tx_last_used;
static unsigned int qn;                 /* 设备队列大小 */
static unsigned int rx_count;           /* 我们实际挂上去的 RX 描述符数 */

static struct vring_desc  *rx_desc;
static struct vring_avail *rx_avail;
static struct vring_used  *rx_used;
static struct vring_desc  *tx_desc;
static struct vring_avail *tx_avail;
static struct vring_used  *tx_used;

static unsigned int phys(const void *p) { return V2P((unsigned int) p); }

static unsigned int align4k(unsigned int v) { return (v + 4095) & ~4095U; }

static void dbg_dec(unsigned int v) {
    char b[12]; int n = 0;
    if (!v) { serial_write_char('0'); return; }
    while (v && n < 11) { b[n++] = (char) ('0' + v % 10); v /= 10; }
    while (n--) serial_write_char(b[n]);
}

static void ring_layout(unsigned char *base, struct vring_desc **d,
                        struct vring_avail **a, struct vring_used **u) {
    *d = (struct vring_desc *) base;
    *a = (struct vring_avail *) (base + 16 * qn);
    *u = (struct vring_used *)  (base + align4k(16 * qn + 6 + 2 * qn));
}

int virtio_net_init(void) {
    int b, d, f, i;
    unsigned int bar0, features;

    if (!pci_find_id(0x1AF4, 0x1000, &b, &d, &f) &&
        !pci_find_id(0x1AF4, 0x1041, &b, &d, &f)) {
        return 0;
    }
    bar0 = pci_read32(b, d, f, 0x10);
    if (!(bar0 & 1)) {
        serial_write_string("net: virtio-net has no I/O BAR\n");
        return 0;
    }
    io = (unsigned short) (bar0 & 0xFFFC);
    pci_write16(b, d, f, 0x04, 0x0007);      /* I/O + MEM + bus master */

    outb(io + STATUS, 0);
    { unsigned int t = 0; while (inb(io + STATUS) != 0 && t < 1000) t++; }
    outb(io + STATUS, 1);                    /* ACK */
    outb(io + STATUS, 3);                    /* ACK | DRIVER */

    features = inl(io + FEATURES);
    outl(io + GUESTFEAT, features & (1u << 5));   /* 只接受 VIRTIO_NET_F_MAC */
    for (i = 0; i < 6; i++) mac[i] = inb(io + 0x14 + i);

    /* 读设备队列大小（只读），并据此布局 */
    outw(io + QSELECT, 0);
    qn = inw(io + QSIZE_R) & 0x7FFF;
    if (qn < 2 || qn > 1024) { serial_write_string("net: bad queue size\n"); return 0; }
    if (qn > 256) qn = 256;
    serial_write_string("net: queue size ");
    dbg_dec(qn);
    serial_write_char('\n');

    memset(rx_ring, 0, RING_BYTES);
    memset(tx_ring, 0, RING_BYTES);
    ring_layout(rx_ring, &rx_desc, &rx_avail, &rx_used);
    ring_layout(tx_ring, &tx_desc, &tx_avail, &tx_used);

    rx_count = qn < 64 ? qn : 64;
    for (i = 0; i < (int) rx_count; i++) {
        rx_desc[i].addr = phys(rx_buf[i]);
        rx_desc[i].len = 2048;
        rx_desc[i].flags = 2;                /* VRING_DESC_F_WRITE */
        rx_desc[i].next = 0;
        rx_avail->ring[i] = (unsigned short) i;
    }
    rx_avail->idx = (unsigned short) rx_count;

    outw(io + QSELECT, 0);
    outw(io + QSIZE_R, (unsigned short) qn);
    outl(io + QADDR, phys(rx_ring) >> 12);

    outw(io + QSELECT, 1);
    outw(io + QSIZE_R, (unsigned short) qn);
    outl(io + QADDR, phys(tx_ring) >> 12);

    rx_last_used = 0;
    tx_last_used = 0;

    outb(io + STATUS, 7);                    /* ACK | DRIVER | DRIVER_OK */
    outw(io + QNOTIFY, 0);
    ready = 1;
    return 1;
}

int virtio_net_ready(void) { return ready; }
const unsigned char *virtio_net_mac(void) { return mac; }

int virtio_net_send(const void *frame, unsigned int len) {
    unsigned int total, start;
    if (!ready || len > 1500) return -1;

    total = 10 + len;
    memset(tx_buf, 0, 10);
    memcpy(tx_buf + 10, frame, len);

    tx_desc[0].addr = phys(tx_buf);
    tx_desc[0].len = total;
    tx_desc[0].flags = 0;
    tx_desc[0].next = 0;

    tx_avail->ring[tx_avail->idx % qn] = 0;
    tx_avail->idx++;
    outw(io + QNOTIFY, 1);

    start = time_ms();
    while (tx_used->idx == tx_last_used) {
        if (time_ms() - start > 100) return -1;
    }
    tx_last_used++;
    return 0;
}

int virtio_net_recv(void *buf, unsigned int cap, unsigned int *out_len) {
    struct vring_used_elem e;
    unsigned int id, len;

    if (!ready) return 0;
    if (rx_used->idx == rx_last_used) return 0;

    e = rx_used->ring[rx_last_used % qn];
    rx_last_used++;
    id = e.id;
    len = e.len;

    if (id >= rx_count) return 0;

    if (len > 10 && buf && cap) {
        unsigned int n = len - 10;
        if (n > cap) n = cap;
        memcpy(buf, rx_buf[id] + 10, n);
        *out_len = n;
    } else {
        *out_len = 0;
    }

    rx_avail->ring[rx_avail->idx % qn] = (unsigned short) id;
    rx_avail->idx++;
    outw(io + QNOTIFY, 0);
    return 1;
}
