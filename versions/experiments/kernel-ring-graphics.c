/*
 * kernel.c —— 这才是真正意义上"用 C 写"的部分。
 *
 * boot.asm 在进保护模式前已经把显卡切到了 320x200、256 色的图形模式（BIOS 模式 0x13），
 * 显存是一块线性缓冲区，起始地址固定在 0xA0000：每个像素对应一个字节，
 * 字节的值就是这个像素用调色板里第几号颜色——不用自己配色的话，
 * 0 号固定是黑、15 号固定是白，这是 BIOS 初始化这个模式时给的默认调色板，直接能用。
 */

#define VGA_MEMORY ((volatile unsigned char *) 0xA0000)
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 200

#define COLOR_BLACK 0
#define COLOR_WHITE 15

void put_pixel(int x, int y, unsigned char color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;                       /* 越界直接丢弃，别把别的显存/内存写坏 */
    }
    VGA_MEMORY[y * SCREEN_WIDTH + x] = color;
}

void fill_screen(unsigned char color) {
    int i;
    for (i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        VGA_MEMORY[i] = color;
    }
}

/*
 * 画一个居中的空心圆环，当作 zeroOS 现阶段的开机标志：
 * 简单遍历圆外接正方形的每个点，落在"外半径以内、内半径以外"这个环形区域里的才点亮，
 * 320x200 这个分辨率下这种暴力算法完全够用，没必要上更复杂的画圆算法。
 */
void draw_ring(int cx, int cy, int radius, int thickness, unsigned char color) {
    int outer_sq = radius * radius;
    int inner_r  = radius - thickness;
    int inner_sq = inner_r * inner_r;
    int y, x;

    for (y = -radius; y <= radius; y++) {
        for (x = -radius; x <= radius; x++) {
            int dist_sq = x * x + y * y;
            if (dist_sq <= outer_sq && dist_sq >= inner_sq) {
                put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void kmain(void) {
    fill_screen(COLOR_BLACK);                                   /* 纯黑底，不留任何实模式阶段的痕迹 */
    draw_ring(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 30, 4, COLOR_WHITE);  /* 居中的白色圆环 */

    while (1) {
        __asm__ __volatile__("hlt");   /* 没事干就让 CPU 休息，别空转烧电 */
    }
}
