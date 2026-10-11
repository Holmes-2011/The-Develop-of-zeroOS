#ifndef CONSOLE_H
#define CONSOLE_H

#include "vga.h"

/* 控制台：同一份文字同时写到屏幕和串口。
   屏幕给人看；串口给自动测试和调试看（QEMU 能把它存成文件） */

void console_putc(char c);
void console_write(const char *str);
void console_write_dec(unsigned int n);
void console_write_hex(unsigned int n);          /* 0x 开头，固定 8 位 */
void console_set_color(enum vga_color fg);       /* 只影响屏幕，串口没有颜色 */

#endif
