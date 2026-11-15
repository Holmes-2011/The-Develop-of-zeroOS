#include "wm.h"
#include "gfx.h"
#include "mouse.h"
#include "keyboard.h"
#include "console.h"
#include "io.h"

/* 桌面状态：两个窗口 + 鼠标指针 */
static struct win {
    int x, y, w, h;
    const char *title;
    unsigned int bar;
} wins[2] = {
    {  80,  80, 380, 220, "zeroOS v0.19", 0x003366CC },
    { 560, 160, 380, 240, "Mouse & Windows", 0x00206060 },
};
static int focus = 1;          /* 最后画的在最上面 */
static int dragging = -1;
static int drag_dx, drag_dy;

static int mx = GFX_WIDTH / 2, my = GFX_HEIGHT / 2;
static int buttons;
static unsigned int save_bg[8 * 12];
static int save_x, save_y;

/* 8x12 的箭头指针 */
static const unsigned char cursor_bits[12] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xF8, 0xD8, 0x88, 0x08, 0x00
};

static void bg_rect(int x, int y, int w, int h) {
    int i, j;
    for (j = 0; j < h; j++) {
        int yy = y + j;
        unsigned int b;
        unsigned int color;
        if (yy < 0 || yy >= GFX_HEIGHT) continue;
        b = (unsigned int) (yy * 255 / GFX_HEIGHT);
        color = (b << 16) | (b << 8) | (b / 2);
        for (i = 0; i < w; i++) {
            int xx = x + i;
            if (xx >= 0 && xx < GFX_WIDTH) {
                gfx_putpixel_safe(xx, yy, color);
            }
        }
    }
}

static void draw_window(int idx) {
    struct win *w = &wins[idx];
    int x = w->x, y = w->y;

    /* 边框 + 标题栏 + 主体 */
    gfx_fill_rect(x - 2, y - 2, w->w + 4, w->h + 4, idx == focus ? 0x00FFFFFF : 0x00808080);
    gfx_fill_rect(x, y, w->w, 22, w->bar);
    gfx_fill_rect(x, y + 22, w->w, w->h - 22, 0x00E8E8E8);
    gfx_text_at(x + 8, y + 3, w->title, 15, 0);

    if (idx == 1) {
        gfx_text_at(x + 12, y + 40, "mouse: move / click / drag title bar", 0, 15);
        gfx_text_at(x + 12, y + 64, "ESC: back to shell", 0, 15);
    } else {
        gfx_text_at(x + 12, y + 40, "framebuffer 1024x768x32", 0, 15);
        gfx_text_at(x + 12, y + 64, "two windows, focus + drag", 0, 15);
    }
}

static void draw_cursor(void) {
    int i, b;
    save_x = mx;
    save_y = my;
    for (i = 0; i < 12; i++) {
        for (b = 0; b < 8; b++) {
            int xx = mx + b, yy = my + i;
            if (xx < GFX_WIDTH && yy < GFX_HEIGHT) save_bg[i * 8 + b] = gfx_get_pixel(xx, yy);
        }
    }
    for (i = 0; i < 12; i++) {
        for (b = 0; b < 8; b++) {
            if (cursor_bits[i] & (0x80 >> b)) gfx_putpixel_safe(mx + b, my + i, 0x00000000);
            else if (b == 0 && i < 11) gfx_putpixel_safe(mx + b, my + i, 0x00FFFFFF);
        }
    }
}

static void erase_cursor(void) {
    int i, b;
    for (i = 0; i < 12; i++)
        for (b = 0; b < 8; b++)
            gfx_putpixel_safe(save_x + b, save_y + i, save_bg[i * 8 + b]);
}

static void redraw_all(void) {
    bg_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
    draw_window(1);
    draw_window(0);
    draw_cursor();
}

static int hit(int x, int y) {
    int i;
    /* 从最上层（focus）往下找 */
    if (x >= wins[focus].x && x < wins[focus].x + wins[focus].w &&
        y >= wins[focus].y && y < wins[focus].y + wins[focus].h) return focus;
    i = 1 - focus;
    if (x >= wins[i].x && x < wins[i].x + wins[i].w &&
        y >= wins[i].y && y < wins[i].y + wins[i].h) return i;
    return -1;
}

static void clamp_win(struct win *w) {
    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
    if (w->x + w->w > GFX_WIDTH) w->x = GFX_WIDTH - w->w;
    if (w->y + w->h > GFX_HEIGHT) w->y = GFX_HEIGHT - w->h;
}

void wm_run(void) {
    int prev_buttons = 0;

    if (!gfx_active()) {
        console_write("desktop: no graphics mode\n");
        return;
    }
    mx = GFX_WIDTH / 2;
    my = GFX_HEIGHT / 2;
    focus = 1;
    dragging = -1;
    buttons = 0;

    redraw_all();

    for (;;) {
        int c = keyboard_getchar();
        if (c == 27) break;                 /* ESC */

        {
            int dx, dy, btn;
            if (mouse_poll(&dx, &dy, &btn)) {
                erase_cursor();
                mx += dx;
                my += dy;
                if (mx < 0) mx = 0;
                if (my < 0) my = 0;
                if (mx >= GFX_WIDTH) mx = GFX_WIDTH - 1;
                if (my >= GFX_HEIGHT) my = GFX_HEIGHT - 1;

                if ((btn & 1) && !(prev_buttons & 1)) {      /* 按下 */
                    int w = hit(mx, my);
                    if (w >= 0) {
                        int changed_focus = (w != focus);
                        focus = w;
                        if (my < wins[w].y + 22) {            /* 标题栏 → 按下就开始拖 */
                            dragging = w;
                            drag_dx = mx - wins[w].x;
                            drag_dy = my - wins[w].y;
                        }
                        if (changed_focus || dragging >= 0) redraw_all();
                    }
                } else if (!(btn & 1) && (prev_buttons & 1)) { /* 松开 */
                    dragging = -1;
                }

                if (dragging >= 0 && (btn & 1)) {
                    struct win *w = &wins[dragging];
                    int oldx = w->x, oldy = w->y;
                    w->x = mx - drag_dx;
                    w->y = my - drag_dy;
                    clamp_win(w);
                    bg_rect(oldx - 2, oldy - 2, w->w + 4, w->h + 4);   /* 旧位置（含边框）补背景 */
                    bg_rect(w->x - 2, w->y - 2, w->w + 4, w->h + 4);   /* 新位置（含边框）补背景 */
                    draw_window(1 - dragging);
                    draw_window(dragging);
                    draw_cursor();
                } else {
                    draw_cursor();
                }
                prev_buttons = btn;
                buttons = btn;
            }
        }
        __asm__ __volatile__("sti; hlt");
    }

    erase_cursor();
    gfx_clear(0);
    console_write("\ndesktop: closed (ESC)\n");
}

void wm_info(void) {
    console_write("wm: win0=");
    console_write_dec((unsigned int) wins[0].x); console_write(",");
    console_write_dec((unsigned int) wins[0].y);
    console_write(" win1=");
    console_write_dec((unsigned int) wins[1].x); console_write(",");
    console_write_dec((unsigned int) wins[1].y);
    console_write(" focus=");
    console_write_dec((unsigned int) focus);
    console_write("\n");
}
