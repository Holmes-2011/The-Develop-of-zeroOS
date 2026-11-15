#include "usb.h"
#include "pci.h"
#include "io.h"
#include "paging.h"
#include "kstring.h"
#include "serial.h"
#include "time.h"

static void dbg_hex(unsigned int v) {
    const char *hex = "0123456789ABCDEF";
    int i;
    serial_write_string("0x");
    for (i = 28; i >= 0; i -= 4) serial_write_char(hex[(v >> i) & 0xF]);
}


/* UHCI 寄存器（I/O 空间） */
#define UHCI_CMD    0x00
#define UHCI_STS    0x02
#define UHCI_INTR   0x04
#define UHCI_FRNUM  0x06
#define UHCI_FLBASE 0x08
#define UHCI_SOF    0x0C
#define UHCI_PORT1  0x10

#define TD_ACTIVE   (1u << 23)
#define TD_IOC      (1u << 24)
#define TD_ERR      0x007F8000u     /* stall / buffer / babble / nak / crc / bitstuff / timeout */

#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1

static unsigned int io_base;
static int  dev_addr;
static int  ep0_max = 8;
static int  int_ep = -1;
static int  int_toggle;
static int  ready;

static unsigned int frame_list[1024] __attribute__((aligned(4096)));
static unsigned int qh[2][8]         __attribute__((aligned(16)));
static unsigned int td[8][8]         __attribute__((aligned(16)));
static unsigned char setup_pkt[8]    __attribute__((aligned(16)));
static unsigned char data_buf[256]   __attribute__((aligned(16)));
static unsigned char hid_buf[8]      __attribute__((aligned(16)));

static unsigned int phys(void *p) { return V2P((unsigned int) p); }

static void td_build(int i, unsigned int link, int active, int ioc,
                     int pid, int addr, int ep, int toggle, int len, unsigned int buf) {
    td[i][0] = link;
    td[i][1] = (active ? TD_ACTIVE : 0) | (ioc ? TD_IOC : 0) | (3u << 27);   /* 允许 3 次重试 */
    {
        /* UHCI 的“最大长度”字段是 长度-1；0 长度要写 0x7FF（+1 后溢出成 0） */
        unsigned int ml = len ? ((unsigned int) (len - 1) & 0x7FF) : 0x7FF;
        td[i][2] = (unsigned int) (pid & 0xFF) | ((unsigned int) (addr & 0x7F) << 8) |
                   ((unsigned int) (ep & 0xF) << 15) | ((unsigned int) (toggle & 1) << 19) |
                   (ml << 21);
    }
    td[i][3] = buf;
}

static void delay_ms(unsigned int ms) {
    unsigned int start = time_ms();
    while (time_ms() - start < ms) __asm__ __volatile__("pause");
}

static int wait_td(int i, unsigned int timeout_ms) {
    unsigned int start = time_ms();
    while (td[i][1] & TD_ACTIVE) {
        if (time_ms() - start > timeout_ms) return -1;
    }
    return 0;
}

/* 控制传输：in=1 表示设备→主机 */
static int usb_control(int in, unsigned char req, unsigned short val,
                       unsigned short idx, unsigned short len, unsigned char *buf) {
    int last;

    memset(setup_pkt, 0, 8);
    setup_pkt[0] = (unsigned char) (in ? 0x80 : 0x00);
    setup_pkt[1] = req;
    setup_pkt[2] = (unsigned char) (val & 0xFF);
    setup_pkt[3] = (unsigned char) (val >> 8);
    setup_pkt[4] = (unsigned char) (idx & 0xFF);
    setup_pkt[5] = (unsigned char) (idx >> 8);
    setup_pkt[6] = (unsigned char) (len & 0xFF);
    setup_pkt[7] = (unsigned char) (len >> 8);

    td_build(0, phys(td[1]), 1, 0, PID_SETUP, dev_addr, 0, 0, 8, phys(setup_pkt));

    if (len) {
        td_build(1, phys(td[2]), 1, 0, in ? PID_IN : PID_OUT, dev_addr, 0, 1, len, phys(buf));
        td_build(2, 1, 1, 1, in ? PID_OUT : PID_IN, dev_addr, 0, 1, 0, 0);
        last = 2;
    } else {
        /* 无数据控制传输：状态阶段是 IN（设备回 ZLP） */
        td_build(1, 1, 1, 1, PID_IN, dev_addr, 0, 1, 0, 0);
        last = 1;
    }
    qh[0][1] = phys(td[0]);

    if (wait_td(last, 300) != 0) {
        qh[0][1] = 1;
        serial_write_string("usb: ctl timeout sts0=");
        dbg_hex(td[0][1]);
        serial_write_string(" sts1=");
        dbg_hex(td[1][1]);
        serial_write_string(" sts2=");
        dbg_hex(td[2][1]);
        serial_write_string(" tok=");
        dbg_hex(td[last][2]);
        serial_write_char('\n');
        return -1;
    }
    qh[0][1] = 1;

    if (td[last][1] & TD_ERR) {
        serial_write_string("usb: ctl error sts=");
        dbg_hex(td[last][1]);
        serial_write_char('\n');
        return -2;
    }
    return 0;
}

static int get_descriptor(unsigned char type, unsigned char index,
                          unsigned short len, unsigned char *buf) {
    return usb_control(1, 0x06, (unsigned short) ((type << 8) | index), 0, len, buf);
}

static int set_address(int addr) {
    return usb_control(0, 0x05, (unsigned short) addr, 0, 0, 0);
}

static int set_config(int cfg) {
    return usb_control(0, 0x09, (unsigned short) cfg, 0, 0, 0);
}

static int set_idle(void) {
    return usb_control(0, 0x0A, 0, 0, 0, 0);
}

static int port_reset(void) {
    unsigned int sc = inw(io_base + UHCI_PORT1);
    if (!(sc & 0x0001)) return 0;             /* 没设备 */

    outw(io_base + UHCI_PORT1, 0x0200);       /* PR = 1 */
    delay_ms(60);
    outw(io_base + UHCI_PORT1, 0x0000);       /* 清 PR；同时清变更位 */
    delay_ms(20);

    sc = inw(io_base + UHCI_PORT1);
    if (!(sc & 0x0001)) return 0;
    sc |= 0x0004;                              /* 使能端口，保留其它位 */
    outw(io_base + UHCI_PORT1, sc);
    delay_ms(20);
    sc = inw(io_base + UHCI_PORT1);
    return (sc & 0x0004) ? 1 : 0;
}

int usb_tablet_init(void) {
    int b, d, f, i;
    unsigned char dev_desc[18];
    unsigned char cfg_head[9];
    unsigned short total;
    int off;

    if (!pci_find_class(0x0C, 0x03, 0x00, &b, &d, &f)) {
        serial_write_string("usb: no UHCI controller\n");
        return 0;
    }
    io_base = pci_read32(b, d, f, 0x20) & 0xFFE0;
    serial_write_string("usb: UHCI pci=");
    serial_write_char((char) ('0' + b));
    serial_write_char(':');
    serial_write_char((char) ('0' + d / 10));
    serial_write_char((char) ('0' + d % 10));
    serial_write_char('.');
    serial_write_char((char) ('0' + f));
    serial_write_string(" I/O ");
    dbg_hex(io_base);
    serial_write_char('\n');

    outw(io_base + UHCI_CMD, 0x0000);
    outw(io_base + UHCI_CMD, 0x0002);         /* HC 复位 */
    { int t = 0; while ((inw(io_base + UHCI_CMD) & 0x0002) && t < 100) { delay_ms(1); t++; } }
    delay_ms(10);

    for (i = 0; i < 1024; i++) frame_list[i] = phys(qh[0]) | 2;
    qh[0][0] = phys(qh[1]) | 2;  /* 控制 QH → 中断 QH */
    qh[0][1] = 1;
    qh[1][0] = 1;
    qh[1][1] = 1;

    outl(io_base + UHCI_FLBASE, phys(frame_list));
    outb(io_base + UHCI_SOF, 64);
    outw(io_base + UHCI_CMD, 0x0001);         /* RS = 1 */
    delay_ms(20);



    if (!port_reset()) {
        serial_write_string("usb: no device on port 1\n");
        return 0;
    }
    serial_write_string("usb: port 1 device connected\n");

    dev_addr = 0;
    /* 先直接 SET_ADDRESS(1)：有些 BIOS/UHCI 组合下设备在地址 0 上读描述符会 stall */
    if (set_address(1) != 0) {
        serial_write_string("usb: SET_ADDRESS(1) failed\n");
        return 0;
    }
    dev_addr = 1;
    delay_ms(100);
    { int tries;
      for (tries = 0; tries < 3; tries++) {
          if (get_descriptor(1, 0, 8, data_buf) == 0) break;
          serial_write_string("usb: GET_DESC(8) retry\n");
          delay_ms(50);
      }
      if (tries == 3) { serial_write_string("usb: GET_DESC(8) @1 failed\n"); return 0; }
    }
    ep0_max = data_buf[7] ? data_buf[7] : 8;
    serial_write_string("usb: ep0 max packet ");
    serial_write_char((char) ('0' + (ep0_max / 10) % 10));
    serial_write_char((char) ('0' + ep0_max % 10));
    serial_write_char('\n');

    if (get_descriptor(1, 0, 18, dev_desc) != 0) {
        serial_write_string("usb: GET_DESC(18) failed\n");
        return 0;
    }
    serial_write_string("usb: vid/pid ");
    dbg_hex((unsigned int) dev_desc[8] | ((unsigned int) dev_desc[9] << 8));
    serial_write_char('/');
    dbg_hex((unsigned int) dev_desc[10] | ((unsigned int) dev_desc[11] << 8));
    serial_write_char('\n');

    if (get_descriptor(2, 0, 9, cfg_head) != 0) {
        serial_write_string("usb: GET_CONFIG(9) failed\n");
        return 0;
    }
    total = (unsigned short) (cfg_head[2] | (cfg_head[3] << 8));
    if (total > sizeof(data_buf)) total = sizeof(data_buf);
    if (get_descriptor(2, 0, total, data_buf) != 0) {
        serial_write_string("usb: GET_CONFIG(full) failed\n");
        return 0;
    }

    /* 解析配置描述符：找中断 IN 端点 */
    off = 0;
    while (off + 2 <= (int) total) {
        unsigned char len = data_buf[off];
        unsigned char type = data_buf[off + 1];
        if (!len) break;
        if (type == 5 && off + 7 <= (int) total) {         /* endpoint */
            unsigned char ep_addr = data_buf[off + 2];
            unsigned char attr = data_buf[off + 3];
            if ((ep_addr & 0x80) && (attr & 0x03) == 3) {  /* 中断 IN */
                int_ep = ep_addr & 0x0F;
                serial_write_string("usb: interrupt IN endpoint ");
                dbg_hex(ep_addr);
                serial_write_char('\n');
                break;
            }
        }
        off += len;
    }

    if (set_config(1) != 0) {
        serial_write_string("usb: SET_CONFIG failed\n");
        return 0;
    }
    set_idle();

    if (int_ep < 0) {
        serial_write_string("usb: no interrupt endpoint\n");
        return 0;
    }

    int_toggle = 0;
    td_build(3, 1, 1, 1, PID_IN, dev_addr, int_ep, int_toggle, 8, phys(hid_buf));
    qh[1][1] = phys(td[3]);
    ready = 1;
    serial_write_string("usb: tablet ready\n");
    return 1;
}

int usb_tablet_poll(int *x, int *y, int *btn) {
    unsigned int st;
    if (!ready) return 0;
    st = td[3][1];
    if (st & TD_ACTIVE) return 0;

    if (!(st & TD_ERR)) {
        *btn = hid_buf[0] & 0x07;
        *x = ((int) hid_buf[1] | ((int) hid_buf[2] << 8)) * 1023 / 32767;
        *y = ((int) hid_buf[3] | ((int) hid_buf[4] << 8)) * 767 / 32767;
    } else {
        *btn = 0;
    }

    { static int dbg; if (dbg < 8) { dbg++;
        serial_write_string("usb: abs ");
        dbg_hex((unsigned int) *x); serial_write_char(',');
        dbg_hex((unsigned int) *y); serial_write_char(' ');
        dbg_hex((unsigned int) ((hid_buf[1]) | (hid_buf[2] << 8))); serial_write_char(' ');
        dbg_hex((unsigned int) ((hid_buf[3]) | (hid_buf[4] << 8)));
        serial_write_char('\n'); } }

    int_toggle ^= 1;
    td_build(3, 1, 1, 1, PID_IN, dev_addr, int_ep, int_toggle, 8, phys(hid_buf));
    qh[1][1] = phys(td[3]);
    return 1;
}
