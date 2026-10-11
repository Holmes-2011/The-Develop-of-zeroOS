#ifndef SERIAL_H
#define SERIAL_H

/* COM1 串口（v0.2 起就有，v0.8 从 kernel.c 里拆出来）。
   用轮询方式发送，不用中断；QEMU 的 -serial 会把它接到终端或文件，是最可靠的调试输出 */

void serial_init(void);
void serial_write_char(char c);
void serial_write_string(const char *str);

#endif
