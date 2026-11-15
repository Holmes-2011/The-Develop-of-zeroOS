#ifndef GFX_H
#define GFX_H

/* 图形模式（v0.18）：VBE 1024x768x32 线性帧缓冲。
   boot.asm 里切好模式，把 LFB 物理地址写在 0x4500、8x16 字库拷到 0x8000；
   内核在分页打开前把这两样读走，再把 LFB 映射到 GFX_VBASE。
   文字控制台同时画到帧缓冲（vga.c 的 0xB8000 影子照旧，测试不受影响）。 */

#define GFX_WIDTH   1024
#define GFX_HEIGHT  768
#define GFX_PITCH   4096                 /* 1024 * 4 */
#define GFX_VBASE   0xFD000000U          /* LFB 映射到的虚拟地址（0xFC000000 以上是空闲区） */

void gfx_early_init(void);               /* 分页打开前调用：拷字库、记下 LFB 物理地址 */
void gfx_init(void);                     /* 映射 LFB、清屏 */
int  gfx_active(void);

void gfx_clear(unsigned int color);
void gfx_putc(char c, unsigned char fg, unsigned char bg);   /* 跟控制台一样处理 \n \r \b \t */
void gfx_demo(void);                     /* 渐变 + 矩形 + 圆 + 文字 */
void gfx_background(void);               /* 开机默认背景：深色渐变 + 底部信息条 */
void gfx_set_cursor(int c, int r);       /* 把文字光标放到第 c 列、第 r 行 */
void gfx_text_at(int x, int y, const char *s, unsigned char fg, unsigned char bg); /* 任意像素位置画字 */
void gfx_fill_rect(int x, int y, int w, int h, unsigned int color);                 /* 实心矩形 */
void gfx_putpixel_safe(int x, int y, unsigned int color);                           /* 带裁剪的点 */
unsigned int gfx_get_pixel(int x, int y);                                           /* 读回一个点 */

#endif
