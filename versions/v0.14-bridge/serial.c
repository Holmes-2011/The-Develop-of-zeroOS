#include "serial.h"
#include "io.h"

#define COM1 0x3F8

/* 接收环形缓冲区：中断写 head，主循环读 tail（单写单读，不用锁） */
#define RX_SIZE 1024
static volatile unsigned char rx[RX_SIZE];
static volatile unsigned int rx_head, rx_tail;
static volatile unsigned int rx_dropped;

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* 先关掉串口中断，初始化完再按需打开 */
    outb(COM1 + 3, 0x80);   /* 打开 DLAB，接下来两次写入是在设置波特率 */
    outb(COM1 + 0, 0x01);   /* 波特率除数 1 —— 115200（v0.14 起，协议文档规定；以前是 38400） */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 位数据位、无校验、1 位停止位，关闭 DLAB */
    outb(COM1 + 2, 0xC7);   /* 打开 FIFO，并清空收发缓冲区 */
    outb(COM1 + 4, 0x0B);   /* DTR + RTS + OUT2（OUT2 不置上的话 PC 的串口中断到不了 PIC） */
}

void serial_enable_rx(void) {
    outb(COM1 + 1, 0x01);   /* IER：只开"收到数据"中断 */
}

static int serial_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;   /* 第5位=1代表发送缓冲区空了，可以继续写下一个字节 */
}

static int at_line_start = 1;

void serial_write_char(char c) {
    while (!serial_transmit_empty()) { }   /* 缓冲区没空之前先等着，别把字节冲掉 */
    outb(COM1, c);
    at_line_start = (c == '\n');
}

void serial_write_string(const char *str) {
    while (*str) {
        serial_write_char(*str);
        str++;
    }
}

int serial_at_line_start(void) { return at_line_start; }

void serial_begin_record(void) {
    if (!at_line_start) serial_write_char('\n');
}

/* IRQ4：把 FIFO 里所有已到的字节都搬进缓冲区（一次中断可能对应好几个字节） */
void serial_irq(void) {
    while (inb(COM1 + 5) & 0x01) {           /* LSR 第 0 位：有数据 */
        unsigned char c = inb(COM1);
        unsigned int next = (rx_head + 1) % RX_SIZE;
        if (next == rx_tail) { rx_dropped++; continue; }   /* 满了：丢掉新字节，不覆盖没读的 */
        rx[rx_head] = c;
        rx_head = next;
    }
}

int serial_read_char(void) {
    unsigned char c;
    if (rx_tail == rx_head) return -1;
    c = rx[rx_tail];
    rx_tail = (rx_tail + 1) % RX_SIZE;
    return c;
}

unsigned int serial_rx_dropped(void) { return rx_dropped; }
