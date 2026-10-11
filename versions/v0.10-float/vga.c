#include "vga.h"
#include "io.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static volatile unsigned short *const vram = (volatile unsigned short *) 0xB8000;
static int row, col;
static unsigned char attr = VGA_LIGHT_GREY;   /* 默认：黑底浅灰字，和 BIOS 一样 */

static unsigned short cell(char c) {
    return (unsigned short) ((unsigned char) c | (attr << 8));
}

/* 硬件光标（那个闪烁的下划线）由显卡自己画，我们只需要告诉它位置：
   CRT 控制器的 0x0E / 0x0F 号寄存器分别是位置的高 / 低字节 */
static void update_cursor(void) {
    unsigned short pos = (unsigned short) (row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char) (pos >> 8));
}

/* 所有行往上挪一行，最后一行清空 */
static void scroll(void) {
    int i;
    for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vram[i] = vram[i + VGA_WIDTH];
    }
    for (i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vram[i] = cell(' ');
    }
    row = VGA_HEIGHT - 1;
}

void vga_clear(void) {
    int i;
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vram[i] = cell(' ');
    }
    row = 0;
    col = 0;
    update_cursor();
}

void vga_set_color(enum vga_color fg, enum vga_color bg) {
    attr = (unsigned char) ((bg << 4) | fg);
}

void vga_putc(char c) {
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\b') {
        if (col > 0) col--;          /* 只移动光标；"擦掉"由调用方写 "\b \b" 完成，和终端一样 */
    } else if (c == '\t') {
        col = (col + 8) & ~7;
    } else if (c >= ' ' && c <= '~') {
        vram[row * VGA_WIDTH + col] = cell(c);
        col++;
    }

    if (col >= VGA_WIDTH) {          /* 写满一行自动换行 */
        col = 0;
        row++;
    }
    if (row >= VGA_HEIGHT) {
        scroll();
    }
    update_cursor();
}
