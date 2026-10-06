#ifndef IO_H
#define IO_H

/* 往某个 I/O 端口写一个字节 / 从某个 I/O 端口读一个字节——
   PIC、串口、以后的键盘控制器都要靠这两个最基本的操作 */

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#endif
