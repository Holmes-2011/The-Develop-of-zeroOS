#ifndef SERIAL_H
#define SERIAL_H

/* COM1 串口（v0.2 起就有，v0.8 从 kernel.c 里拆出来）。
   发送用轮询；接收（v0.14）用 IRQ4 + 环形缓冲区，和键盘一样的结构：
   中断只往缓冲区里放字节，主循环再取出来拼成行。115200 8N1 */

void serial_init(void);                 /* 开中断之前调用；之后再 serial_enable_rx() */
void serial_enable_rx(void);            /* 放开接收中断（IRQ4 已 unmask 之后） */
void serial_irq(void);                  /* IRQ4：把收到的字节放进缓冲区 */
int  serial_read_char(void);            /* 取一个收到的字节；没有返回 -1。只能在主循环里调用 */
unsigned int serial_rx_dropped(void);   /* 缓冲区满丢掉的字节数 */
void serial_write_char(char c);
void serial_write_string(const char *str);

/* v0.13.2：串口上同时有控制台文字和机器读的行（遥测流、以后的协议行）。
   机器读的行必须从行首开始，否则会和正在回显的半行文字粘在一起、解析器丢行。
   发这类行之前先调 serial_begin_record()：不在行首就先换行 */
int  serial_at_line_start(void);
void serial_begin_record(void);

#endif
