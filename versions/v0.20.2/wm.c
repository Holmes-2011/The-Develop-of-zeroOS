#include "wm.h"
#include "gfx.h"
#include "mouse.h"
#include "usb.h"
#include "keyboard.h"
#include "console.h"
#include "time.h"

/* v0.21：合成器桌面。窗口画在后台缓冲里，指针画在前台；
   每次只把脏区从后台拷到前台，拖动时不会出现中间帧。 */

struct win {
    int x, y, w, h;
    const char *title;
    unsigned int accent;
    int visible;
};

static struct win wins[2] = {
    {  90,  86, 400, 230, "About zeroOS", 0x002F6BD8, 1 },
    { 560, 180, 380, 240, "Mouse & Windows", 0x003A8C7A, 1 },
};
static int focus = 1;
static int dragging = -1;
static int drag_dx, drag_dy;
static int mx = GFX_WIDTH / 2, my = GFX_HEIGHT / 2;
static int buttons;
static int prev_buttons;

#define TASKBAR_H 28
#define SHADOW    6

/* ---- 基础绘制（都受 clip 限制，画到后台缓冲） ---- */

static void bg_rect(int x, int y, int w, int h) {
    int i, j;
    for (j = 0; j < h; j++) {
        int yy = y + j;
        unsigned int b, color;
        if (yy < 0 || yy >= GFX_HEIGHT) continue;
        b = (unsigned int) (yy * 120 / GFX_HEIGHT);
        color = ((b / 2) << 16) | ((b / 2 + 8) << 8) | (b + 24);
        for (i = 0; i < w; i++) gfx_putpixel_safe(x + i, yy, color);
    }
}

static void draw_taskbar(void) {
    int i;
    gfx_fill_rect(0, GFX_HEIGHT - TASKBAR_H, GFX_WIDTH, TASKBAR_H, 0x00161B22);
    gfx_fill_rect(0, GFX_HEIGHT - TASKBAR_H, GFX_WIDTH, 1, 0x00343D4A);
    gfx_text_at(10, GFX_HEIGHT - TASKBAR_H + 6, "zeroOS v0.20.2", 0x00E6E9EE, 0x00161B22);

    /* 每个窗口一个任务栏按钮 */
    for (i = 0; i < 2; i++) {
        int bx = 150 + i * 150;
        unsigned int c = (i == focus) ? wins[i].accent : 0x00262E3A;
        gfx_fill_rect(bx, GFX_HEIGHT - TASKBAR_H + 5, 140, TASKBAR_H - 10, c);
        gfx_text_at(bx + 6, GFX_HEIGHT - TASKBAR_H + 6, wins[i].title, 0x00E6E9EE, c);
    }

    /* 右侧：退出按钮（鼠标可点，防止键盘在虚拟机里不管用） */
    gfx_fill_rect(GFX_WIDTH - 66, GFX_HEIGHT - TASKBAR_H + 4, 58, TASKBAR_H - 8, 0x00903A3A);
    gfx_text_at(GFX_WIDTH - 58, GFX_HEIGHT - TASKBAR_H + 6, "Exit", 0x00FFFFFF, 0x00903A3A);

    /* 开机时长 */
    {
        unsigned int sec = time_ms() / 1000;
        char buf[16];
        int n = 0;
        unsigned int mm = sec / 60, ss = sec % 60;
        buf[n++] = 'u'; buf[n++] = 'p'; buf[n++] = ' ';
        if (mm >= 10) buf[n++] = (char) ('0' + (mm / 10) % 10);
        buf[n++] = (char) ('0' + mm % 10);
        buf[n++] = ':';
        buf[n++] = (char) ('0' + (ss / 10) % 6);
        buf[n++] = (char) ('0' + ss % 10);
        buf[n] = 0;
        gfx_text_at(GFX_WIDTH - 150, GFX_HEIGHT - TASKBAR_H + 6, buf, 0x008FB3C9, 0x00161B22);
    }
}

static void draw_window(int idx) {
    struct win *w = &wins[idx];
    int x = w->x, y = w->y;
    int i;

    /* 阴影 */
    for (i = 0; i < SHADOW; i++) {
        gfx_fill_rect(x - 2 + i, y + w->h - 2 + i, w->w + 4, 2, 0x0010151C);
    }
    gfx_fill_rect(x + w->w, y + 8, SHADOW, w->h - 8, 0x0010151C);

    /* 边框 */
    gfx_fill_rect(x - 1, y - 1, w->w + 2, w->h + 2, idx == focus ? 0x00D8DEE9 : 0x006A7480);
    /* 标题栏：双色渐变，做出一点立体感 */
    for (i = 0; i < 26; i++) {
        unsigned int a = w->accent, b = (a >> 1) & 0x007F7F7F;
        unsigned int c = ((((a >> 16) & 0xFF) * (26 - i) + ((b >> 16) & 0xFF) * i) / 26) << 16 |
                         ((((a >> 8) & 0xFF) * (26 - i) + ((b >> 8) & 0xFF) * i) / 26) << 8 |
                         (((a & 0xFF) * (26 - i) + (b & 0xFF) * i) / 26);
        gfx_fill_rect(x, y + i, w->w, 1, c);
    }
    /* 关闭按钮 */
    gfx_fill_rect(x + w->w - 20, y + 7, 12, 12, 0x00D94F4F);
    gfx_text_at(x + w->w - 18, y + 4, "x", 0x00FFFFFF, 0x00D94F4F);

    gfx_text_at(x + 10, y + 5, w->title, 0x00FFFFFF, w->accent);

    /* 正文 */
    gfx_fill_rect(x, y + 26, w->w, w->h - 26, 0x00ECEEF2);
    if (idx == 1) {
        gfx_text_at(x + 14, y + 44, "move  -  drag title bar", 0x00101418, 0x00ECEEF2);
        gfx_text_at(x + 14, y + 68, "click  -  focus / raise", 0x00101418, 0x00ECEEF2);
        gfx_text_at(x + 14, y + 92, "ESC    -  back to shell", 0x00101418, 0x00ECEEF2);
        gfx_text_at(x + 14, y + 124, "USB tablet or PS/2 mouse", 0x00666F7A, 0x00ECEEF2);
    } else {
        gfx_text_at(x + 14, y + 44, "framebuffer 1024x768x32", 0x00101418, 0x00ECEEF2);
        gfx_text_at(x + 14, y + 68, "double buffered compositor", 0x00101418, 0x00ECEEF2);
        gfx_text_at(x + 14, y + 92, "no flicker while dragging", 0x00101418, 0x00ECEEF2);
        gfx_text_at(x + 14, y + 124, "taskbar at the bottom", 0x00666F7A, 0x00ECEEF2);
    }
}

static void scene_rect(int x, int y, int w, int h) {
    gfx_set_clip(x, y, w, h);
    bg_rect(x, y, w, h);
    draw_window(1);
    draw_window(0);
    draw_taskbar();
    gfx_reset_clip();
}

/* 指针直接画在前台，不进入后台缓冲 */
static void draw_cursor_front(void) {
    static const unsigned char arrow[12] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xF8, 0xD8, 0x88, 0x08, 0x00
    };
    int i, b;
    for (i = 0; i < 12; i++) {
        for (b = 0; b < 8; b++) {
            if (arrow[i] & (0x80 >> b)) gfx_front_pixel(mx + b, my + i, 0x00000000);
            else if (b == 0 && i < 11) gfx_front_pixel(mx + b, my + i, 0x00FFFFFF);
        }
    }
}

static void present_rect(int x, int y, int w, int h) {
    gfx_present(x, y, w, h);
    draw_cursor_front();
}

static void clamp_win(struct win *w) {
    if (w->x < 2) w->x = 2;
    if (w->y < 2) w->y = 2;
    if (w->x + w->w > GFX_WIDTH - 2) w->x = GFX_WIDTH - 2 - w->w;
    if (w->y + w->h > GFX_HEIGHT - TASKBAR_H - 2) w->y = GFX_HEIGHT - TASKBAR_H - 2 - w->h;
}

static int hit(int x, int y) {
    int i;
    if (x >= wins[focus].x && x < wins[focus].x + wins[focus].w &&
        y >= wins[focus].y && y < wins[focus].y + wins[focus].h) return focus;
    i = 1 - focus;
    if (x >= wins[i].x && x < wins[i].x + wins[i].w &&
        y >= wins[i].y && y < wins[i].y + wins[i].h) return i;
    return -1;
}

static void full_redraw(void) {
    gfx_reset_clip();
    scene_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
    gfx_present_all();
    draw_cursor_front();
}

void wm_run(void) {
    unsigned int last_input = time_ms();

    if (!gfx_active()) {
        console_write("desktop: no graphics mode\n");
        return;
    }
    console_write("desktop: ESC / q or the Exit button returns to the shell\n");
    if (!gfx_back_start()) {
        console_write("desktop: no memory for back buffer, running without it\n");
    }

    mx = GFX_WIDTH / 2;
    my = GFX_HEIGHT / 2;
    focus = 1;
    dragging = -1;
    buttons = 0;
    prev_buttons = 0;

    full_redraw();

    for (;;) {
        int c = keyboard_getchar();
        if (c == 27 || c == 'q' || c == 'Q') break;
        if (c >= 0) last_input = time_ms();

        {
            int dx, dy, btn, ax, ay, ab;
            int old_mx = mx, old_my = my;
            int have = 0;

            if (usb_tablet_poll(&ax, &ay, &ab)) {
                mx = ax; my = ay; btn = ab; have = 1;
            } else if (mouse_poll(&dx, &dy, &btn)) {
                mx += dx; my += dy; have = 1;
            }

            if (have) {
                last_input = time_ms();
                if (mx < 0) mx = 0;
                if (my < 0) my = 0;
                if (mx >= GFX_WIDTH) mx = GFX_WIDTH - 1;
                if (my >= GFX_HEIGHT) my = GFX_HEIGHT - 1;

                if ((btn & 1) && !(prev_buttons & 1)) {      /* 按下 */
                    int w;
                    last_input = time_ms();
                    if (mx >= GFX_WIDTH - 66 && my >= GFX_HEIGHT - TASKBAR_H) break;   /* Exit 按钮 */
                    w = hit(mx, my);
                    if (w >= 0) {
                        int changed = (w != focus);
                        focus = w;
                        if (my < wins[w].y + 26) {
                            dragging = w;
                            drag_dx = mx - wins[w].x;
                            drag_dy = my - wins[w].y;
                        }
                        if (changed || dragging >= 0) full_redraw();
                    }
                } else if (!(btn & 1) && (prev_buttons & 1)) {
                    dragging = -1;
                }

                if (dragging >= 0 && (btn & 1)) {
                    struct win *w = &wins[dragging];
                    int old_x = w->x, old_y = w->y;
                    w->x = mx - drag_dx;
                    w->y = my - drag_dy;
                    clamp_win(w);
                    if (w->x != old_x || w->y != old_y) {
                        int x0 = (old_x < w->x ? old_x : w->x) - SHADOW - 3;
                        int y0 = (old_y < w->y ? old_y : w->y) - SHADOW - 3;
                        int x1 = (old_x > w->x ? old_x : w->x) + w->w + SHADOW + 3;
                        int y1 = (old_y > w->y ? old_y : w->y) + w->h + SHADOW + 3;
                        scene_rect(x0, y0, x1 - x0, y1 - y0);
                        present_rect(x0, y0, x1 - x0, y1 - y0);
                    }
                } else if (mx != old_mx || my != old_my) {
                    /* 只移动指针：旧指针区域 + 新指针区域 */
                    int x0 = (old_mx < mx ? old_mx : mx) - 1;
                    int y0 = (old_my < my ? old_my : my) - 1;
                    int x1 = (old_mx > mx ? old_mx : mx) + 10;
                    int y1 = (old_my > my ? old_my : my) + 13;
                    scene_rect(x0, y0, x1 - x0, y1 - y0);
                    present_rect(x0, y0, x1 - x0, y1 - y0);
                }
                prev_buttons = btn;
            }
        }
        if (time_ms() - last_input > 60000) {
            console_write("desktop: no input for 60s, returning to shell\n");
            break;
        }
        __asm__ __volatile__("sti; hlt");
    }

    gfx_reset_clip();
    gfx_back_stop();
    gfx_clear(0);
    console_write("\ndesktop: closed\n");
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
