#include "mouse.h"
#include "io.h"
#include "pit.h"

/* 8042 键盘控制器：0x64 状态/命令，0x60 数据 */
#define PS2_STATUS 0x64
#define PS2_DATA   0x60

static int packet[3];
static int idx;
static volatile int dx_acc, dy_acc, btn;
static volatile int ready;
static volatile int inited;

static int wait_write(void) {
    int i;
    for (i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS) & 0x02)) return 0;   /* 输入缓冲区空 */
    }
    return -1;
}

static int wait_read(void) {
    int i;
    for (i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & 0x01) return 0;      /* 有数据 */
    }
    return -1;
}

static void ctrl_out(unsigned char cmd) {
    wait_write();
    outb(PS2_STATUS, cmd);
}

static void data_out(unsigned char val) {
    wait_write();
    outb(PS2_DATA, val);
}

static unsigned char data_in(void) {
    wait_read();
    return inb(PS2_DATA);
}

void mouse_init(void) {
    unsigned char config;

    ctrl_out(0xA8);                 /* 打开 aux（鼠标）通道 */

    ctrl_out(0x20);                 /* 读配置字节 */
    config = data_in();
    config |= 0x02;                 /* bit1：打开 IRQ12 */
    config &= (unsigned char) ~0x20;/* bit5：允许 aux 时钟 */
    ctrl_out(0x60);
    data_out(config);

    ctrl_out(0xD4);                 /* 之后的字节发给鼠标 */
    data_out(0xF6);                 /* 恢复默认参数 */
    (void) data_in();               /* ACK */

    ctrl_out(0xD4);
    data_out(0xF4);                 /* 打开数据上报 */
    (void) data_in();

    idx = 0;
    dx_acc = dy_acc = 0;
    btn = 0;
    ready = 0;
    inited = 1;
}

void mouse_irq(void) {
    unsigned char status = inb(PS2_STATUS);
    int b;

    if (!(status & 0x01)) return;
    if (!(status & 0x20)) return;          /* 不是 aux 数据（可能是键盘） */

    b = inb(PS2_DATA);
    if (idx == 0 && !(b & 0x08)) return;   /* 同步：第 1 字节 bit3 必须是 1 */
    packet[idx++] = b;
    if (idx < 3) return;
    idx = 0;

    {
        int ddx = packet[1];
        int ddy = packet[2];
        if (packet[0] & 0x10) ddx -= 256;          /* 负数用补码 */
        if (packet[0] & 0x20) ddy -= 256;
        dx_acc += ddx;
        dy_acc -= ddy;                              /* PS/2 的 y 向上为正，屏幕向下为正 */
        btn = packet[0] & 0x07;
        ready = 1;
    }
}

int mouse_poll(int *dx, int *dy, int *buttons) {
    unsigned int flags = irq_save();
    int r = ready;
    if (r) {
        *dx = dx_acc; *dy = dy_acc; *buttons = btn;
        dx_acc = dy_acc = 0;
        ready = 0;
    }
    irq_restore(flags);
    return r;
}

int mouse_ready(void) {
    return inited;
}
