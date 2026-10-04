/*
 * kernel.c —— 这才是真正意义上"用 C 写"的部分。
 *
 * 进入保护模式后没有 BIOS 中断可用了（BIOS 中断是实模式的服务），
 * 所以打印文字不再用 int 0x10，而是直接往显存地址 0xB8000 写字节：
 * 每个字符占 2 字节 —— 第 1 字节是 ASCII 码，第 2 字节是颜色属性。
 */

void print_char(char c, int x, int y, char color) {
    volatile char *video = (volatile char *) 0xb8000;
    int offset = (y * 80 + x) * 2;
    video[offset]     = c;
    video[offset + 1] = color;
}

void print_string(const char *str, int x, int y, char color) {
    int i = 0;
    while (str[i] != '\0') {
        print_char(str[i], x + i, y, color);
        i++;
    }
}

void clear_screen(void) {
    volatile char *video = (volatile char *) 0xb8000;
    int i;
    for (i = 0; i < 80 * 25 * 2; i += 2) {
        video[i]     = ' ';
        video[i + 1] = 0x0f;
    }
}

void kmain(void) {
    clear_screen();   /* 实模式阶段的 BIOS 输出还留在显存里，先清一遍再打印，画面才干净 */
    print_string("Hello from the C kernel, zeroOS says hi!", 0, 0, 0x0f);

    while (1) {
        __asm__ __volatile__("hlt");   /* 没事干就让 CPU 休息，别空转烧电 */
    }
}
