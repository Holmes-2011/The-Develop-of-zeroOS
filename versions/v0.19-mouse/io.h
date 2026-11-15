#ifndef IO_H
#define IO_H

/* 往某个 I/O 端口写一个字节 / 从某个 I/O 端口读一个字节——
   PIC、串口、以后的键盘控制器都要靠这两个最基本的操作 */

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 16 位写：关机时要往 ACPI 的 PM1a 控制寄存器写一个字 */
static inline void outw(unsigned short port, unsigned short val) {
    __asm__ __volatile__ ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* 保存中断状态并关中断 / 恢复原状。给"可能在关中断的上下文里被调用"的代码用，
   比无条件的 cli / sti 安全：原来关着就保持关着 */
static inline unsigned int irq_save(void) {
    unsigned int flags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(unsigned int flags) {
    if (flags & 0x200) __asm__ __volatile__("sti" : : : "memory");   /* 第 9 位 IF：原来是开着的才打开 */
}

#endif
