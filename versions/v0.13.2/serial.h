#ifndef SERIAL_H
#define SERIAL_H

/* COM1 串口（v0.2 起就有，v0.8 从 kernel.c 里拆出来）。
   用轮询方式发送，不用中断；QEMU 的 -serial 会把它接到终端或文件，是最可靠的调试输出 */

void serial_init(void);
void serial_write_char(char c);
void serial_write_string(const char *str);

/* v0.13.2：串口上同时有控制台文字和机器读的行（遥测流、以后的协议行）。
   机器读的行必须从行首开始，否则会和正在回显的半行文字粘在一起、解析器丢行。
   发这类行之前先调 serial_begin_record()：不在行首就先换行 */
int  serial_at_line_start(void);
void serial_begin_record(void);

#endif
