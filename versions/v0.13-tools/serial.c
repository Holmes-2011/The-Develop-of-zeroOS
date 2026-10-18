#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* 先关掉串口中断，咱们用轮询方式，不需要中断 */
    outb(COM1 + 3, 0x80);   /* 打开 DLAB，接下来两次写入是在设置波特率 */
    outb(COM1 + 0, 0x03);   /* 波特率除数低字节 —— 对应 38400 波特率 */
    outb(COM1 + 1, 0x00);   /* 波特率除数高字节 */
    outb(COM1 + 3, 0x03);   /* 8 位数据位、无校验、1 位停止位，关闭 DLAB */
    outb(COM1 + 2, 0xC7);   /* 打开 FIFO，并清空收发缓冲区 */
    outb(COM1 + 4, 0x0B);   /* 打开 IRQ 相关线路（虽然用的是轮询，这几位还是按标准置上） */
}

static int serial_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;   /* 第5位=1代表发送缓冲区空了，可以继续写下一个字节 */
}

void serial_write_char(char c) {
    while (!serial_transmit_empty()) { }   /* 缓冲区没空之前先等着，别把字节冲掉 */
    outb(COM1, c);
}

void serial_write_string(const char *str) {
    while (*str) {
        serial_write_char(*str);
        str++;
    }
}
