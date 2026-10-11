#include "console.h"
#include "serial.h"

void console_putc(char c) {
    vga_putc(c);
    serial_write_char(c);
}

void console_write(const char *str) {
    while (*str) console_putc(*str++);
}

void console_write_dec(unsigned int n) {
    char digits[10];
    int i = 0;
    do {
        digits[i++] = (char) ('0' + n % 10);
        n /= 10;
    } while (n);
    while (i) console_putc(digits[--i]);
}

void console_write_hex(unsigned int n) {
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    console_write("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        console_putc(hex[(n >> shift) & 0xF]);
    }
}

void console_set_color(enum vga_color fg) {
    vga_set_color(fg, VGA_BLACK);
}
