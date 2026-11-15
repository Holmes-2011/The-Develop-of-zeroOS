#include "gfx.h"
#include "paging.h"
#include "console.h"
#include "kstring.h"
#include "pmm.h"

#define GFX_LFB_ADDR  0x4500U      /* boot.asm 写下的 LFB 物理地址 */
#define VBE_INFO_ADDR 0x4400U      /* boot.asm 的 VBE mode info 缓冲区 */
#define FONT_ADDR     0x8000U      /* boot 把 BIOS 8x16 字库拷到这里 */

static unsigned char font[4096];
static unsigned int  lfb_phys;
static volatile unsigned int *fb;
static int  active;
static int  gw = GFX_WIDTH, gh = GFX_HEIGHT, gpitch = GFX_PITCH;

/* v0.21：后台缓冲 */
#define GFX_BACK_VBASE 0xFE000000U
static volatile unsigned int *back_buf;
static int  back_ready;
static int  clip_x0, clip_y0, clip_x1 = GFX_WIDTH, clip_y1 = GFX_HEIGHT;
static int  col, row;
static unsigned char cur_fg = 7, cur_bg = 0;

/* VGA 16 色的 RGB 近似值 */
static const unsigned int palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static inline void putpixel(int x, int y, unsigned int color) {
    volatile unsigned int *dst;
    if (x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1) return;
    if (x < 0 || y < 0 || x >= gw || y >= gh) return;
    dst = back_ready ? back_buf : fb;
    dst[y * (gpitch / 4) + x] = color;
}

static void fill_rect(int x, int y, int w, int h, unsigned int color) {
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            putpixel(x + i, y + j, color);
}

static void draw_rect(int x, int y, int w, int h, unsigned int color) {
    int i;
    for (i = 0; i < w; i++) { putpixel(x + i, y, color); putpixel(x + i, y + h - 1, color); }
    for (i = 0; i < h; i++) { putpixel(x, y + i, color); putpixel(x + w - 1, y + i, color); }
}

static void draw_circle(int cx, int cy, int r, unsigned int color) {
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) putpixel(cx + x, cy + y, color);
}

/* ---- 文字 ---- */

static void scroll_up(void) {
    int y;
    int stride = gpitch / 4;
    for (y = 0; y < gh - 16; y++) {
        memcpy((void *) (fb + y * stride), (void *) (fb + (y + 16) * stride), (unsigned int) gpitch);
    }
    fill_rect(0, gh - 16, gw, 16, palette[cur_bg & 15]);
}

static void gfx_draw_char(char c) {
    int gx = col * 8, gy = row * 16, i, b;
    unsigned int fgc = palette[cur_fg & 15], bgc = palette[cur_bg & 15];
    unsigned char *glyph = &font[(unsigned char) c * 16];

    for (i = 0; i < 16; i++) {
        unsigned char bits = glyph[i];
        for (b = 0; b < 8; b++) {
            putpixel(gx + b, gy + i, (bits & (0x80 >> b)) ? fgc : bgc);
        }
    }
}

void gfx_putc(char c, unsigned char fg, unsigned char bg) {
    if (!active) return;
    cur_fg = fg;
    cur_bg = bg;

    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
        return;
    } else if (c == '\b') {
        if (col > 0) col--;
        putpixel(col * 8, row * 16, palette[bg & 15]);
        return;
    } else if (c == '\t') {
        col = (col + 8) & ~7;
    } else {
        gfx_draw_char(c);
        col++;
        if (col >= gw / 8) { col = 0; row++; }
    }

    if (row >= gh / 16) {
        scroll_up();
        row = gh / 16 - 1;
    }
}

void gfx_clear(unsigned int color) {
    if (!active) return;
    fill_rect(0, 0, gw, gh, color);
    col = 0;
    row = 0;
}

/* ---- 初始化 ---- */

void gfx_early_init(void) {
    unsigned short w, h, pitch;
    unsigned char bpp;

    lfb_phys = *(volatile unsigned int *) GFX_LFB_ADDR;
    if (!lfb_phys) return;

    w     = *(volatile unsigned short *) (VBE_INFO_ADDR + 0x12);
    h     = *(volatile unsigned short *) (VBE_INFO_ADDR + 0x14);
    pitch = *(volatile unsigned short *) (VBE_INFO_ADDR + 0x10);
    bpp   = *(volatile unsigned char *)  (VBE_INFO_ADDR + 0x19);
    if (bpp != 32 || !w || !h || !pitch) { lfb_phys = 0; return; }
    gw = w; gh = h; gpitch = pitch;

    memcpy(font, (void *) FONT_ADDR, sizeof(font));
}

void gfx_init(void) {
    unsigned int i;
    unsigned int pages;

    if (!lfb_phys) return;
    pages = ((unsigned int) gpitch * (unsigned int) gh + 4095) / 4096;
    for (i = 0; i < pages; i++) {
        if (paging_map(GFX_VBASE + i * 4096, lfb_phys + i * 4096,
                       PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            return;
        }
    }
    fb = (volatile unsigned int *) GFX_VBASE;
    active = 1;
    gfx_clear(0);
}

int gfx_active(void) {
    return active;
}

/* ---- 演示画面 ---- */

void gfx_background(void) {
    int x, y;
    if (!active) return;
    for (y = 0; y < gh; y++) {
        unsigned int b = (unsigned int) (y * 120 / gh);      /* 0 ~ 120 的深色 */
        unsigned int color = ((b / 2) << 16) | ((b / 2 + 8) << 8) | (b + 24);
        for (x = 0; x < gw; x++) fb[y * (gpitch / 4) + x] = color;
    }
    fill_rect(0, gh - 28, gw, 28, 0x00101820);
    gfx_set_cursor(0, 0);
}

void gfx_demo(void) {
    int x, y;
    if (!active) {
        console_write("no graphics mode available\n");
        return;
    }

    gfx_clear(0);
    for (y = 0; y < gh; y++) {
        unsigned int b = (unsigned int) (y * 255 / gh);
        unsigned int color = (b << 16) | (b << 8) | (b / 2);
        for (x = 0; x < gw; x++) fb[y * (gpitch / 4) + x] = color;
    }

    fill_rect(60, 60, 320, 180, 0x003366CC);
    fill_rect(80, 80, 280, 140, 0x00CC8844);
    draw_rect(60, 60, 320, 180, 0x00FFFFFF);
    draw_circle(gw - 220, 220, 120, 0x00FFCC33);
    draw_circle(gw - 220, 220, 90, 0x00CC6600);
    fill_rect(0, gh - 40, gw, 40, 0x00202020);

    /* 标题用控制台画（串口也会有一份），画在左上角的空白处 */
    gfx_set_cursor(2, 2);
    console_write("zeroOS v0.20.1 -- framebuffer ");
    console_write_dec((unsigned int) gw);
    console_write("x");
    console_write_dec((unsigned int) gh);
    console_write("x32\n");
    gfx_set_cursor(2, gh / 16 - 2);     /* 之后命令行提示符落在底部，不压着画面 */
}

void gfx_fill_rect(int x, int y, int w, int h, unsigned int color) {
    fill_rect(x, y, w, h, color);
}

void gfx_putpixel_safe(int x, int y, unsigned int color) {
    putpixel(x, y, color);
}

unsigned int gfx_get_pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= gw || y >= gh) return 0;
    return fb[y * (gpitch / 4) + x];
}

void gfx_text_at(int x, int y, const char *s, unsigned int fg, unsigned int bg) {
    unsigned int fgc = fg, bgc = bg;
    int cx = x, cy = y;
    while (*s) {
        unsigned char *glyph = &font[(unsigned char) *s * 16];
        int i, b;
        for (i = 0; i < 16; i++) {
            unsigned char bits = glyph[i];
            for (b = 0; b < 8; b++) {
                putpixel(cx + b, cy + i, (bits & (0x80 >> b)) ? fgc : bgc);
            }
        }
        cx += 8;
        s++;
    }
}

void gfx_set_cursor(int c, int r) {
    col = c; row = r;
}

/* ---- v0.21：双缓冲 ---- */

int gfx_back_start(void) {
    unsigned int pages = ((unsigned int) gpitch * (unsigned int) gh + 4095) / 4096;
    unsigned int i, got = 0;
    if (!active || back_ready) return 1;
    for (i = 0; i < pages; i++) {
        unsigned int pa = pmm_alloc_page();
        if (!pa) break;
        if (paging_map(GFX_BACK_VBASE + i * 4096, pa, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            pmm_free_page(pa);
            break;
        }
        got++;
    }
    if (got != pages) {
        for (i = 0; i < got; i++) {
            unsigned int pa = paging_translate(GFX_BACK_VBASE + i * 4096);
            paging_unmap(GFX_BACK_VBASE + i * 4096);
            if (pa != 0xFFFFFFFFU) pmm_free_page(pa);
        }
        return 0;
    }
    back_buf = (volatile unsigned int *) GFX_BACK_VBASE;
    back_ready = 1;
    clip_x0 = 0; clip_y0 = 0; clip_x1 = gw; clip_y1 = gh;
    return 1;
}

void gfx_back_stop(void) {
    unsigned int pages, i;
    if (!back_ready) return;
    pages = ((unsigned int) gpitch * (unsigned int) gh + 4095) / 4096;
    for (i = 0; i < pages; i++) {
        unsigned int pa = paging_translate(GFX_BACK_VBASE + i * 4096);
        paging_unmap(GFX_BACK_VBASE + i * 4096);
        if (pa != 0xFFFFFFFFU) pmm_free_page(pa);
    }
    back_ready = 0;
    clip_x0 = 0; clip_y0 = 0; clip_x1 = gw; clip_y1 = gh;
}

int gfx_has_back(void) {
    return back_ready;
}

void gfx_set_clip(int x, int y, int w, int h) {
    clip_x0 = x < 0 ? 0 : x;
    clip_y0 = y < 0 ? 0 : y;
    clip_x1 = x + w > gw ? gw : x + w;
    clip_y1 = y + h > gh ? gh : y + h;
    if (clip_x1 < clip_x0) clip_x1 = clip_x0;
    if (clip_y1 < clip_y0) clip_y1 = clip_y0;
}

void gfx_reset_clip(void) {
    clip_x0 = 0; clip_y0 = 0; clip_x1 = gw; clip_y1 = gh;
}

void gfx_present(int x, int y, int w, int h) {
    int yy;
    int stride = gpitch / 4;
    if (!back_ready || !active) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > gw) w = gw - x;
    if (y + h > gh) h = gh - y;
    if (w <= 0 || h <= 0) return;
    for (yy = y; yy < y + h; yy++) {
        memcpy((void *) (fb + yy * stride + x), (void *) (back_buf + yy * stride + x), (unsigned int) (w * 4));
    }
}

void gfx_present_all(void) {
    if (back_ready) gfx_present(0, 0, gw, gh);
}

void gfx_front_pixel(int x, int y, unsigned int color) {
    if (!active || x < 0 || y < 0 || x >= gw || y >= gh) return;
    fb[y * (gpitch / 4) + x] = color;
}
