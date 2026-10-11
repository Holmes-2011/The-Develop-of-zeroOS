#ifndef VGA_H
#define VGA_H

/* VGA 文字模式（80×25）屏幕输出。
   BIOS 开机时已经把显卡设成这个模式：显存在 0xB8000，每个字符占 2 字节——
   低字节是字符本身，高字节是颜色（高 4 位背景色，低 4 位前景色） */

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE = 1, VGA_GREEN = 2, VGA_CYAN = 3,
    VGA_RED = 4, VGA_MAGENTA = 5, VGA_BROWN = 6, VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8, VGA_LIGHT_BLUE = 9, VGA_LIGHT_GREEN = 10, VGA_LIGHT_CYAN = 11,
    VGA_LIGHT_RED = 12, VGA_LIGHT_MAGENTA = 13, VGA_YELLOW = 14, VGA_WHITE = 15
};

void vga_clear(void);                       /* 用当前颜色清屏，光标回到左上角 */
void vga_set_color(enum vga_color fg, enum vga_color bg);
void vga_putc(char c);                      /* 支持 \n、\b（光标退一格）、\t；写满最后一行自动上滚 */

#endif
