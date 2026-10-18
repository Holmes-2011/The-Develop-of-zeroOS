#include "confirm.h"
#include "console.h"
#include "keyboard.h"
#include "time.h"
#include "telemetry.h"

static void box_line(const char *s) {
    console_set_colors(VGA_YELLOW, VGA_BLUE);
    console_write(" | ");
    console_write(s);
    console_set_colors(VGA_LIGHT_GREY, VGA_BLACK);
    console_putc('\n');
}

int confirm_ask(const char *action, const char *detail) {
    unsigned int discarded, start, left, last_shown = 0xFFFFFFFF;
    int answer = -1;

    /* 防线 1：先清空键盘缓冲区 */
    discarded = keyboard_flush();

    console_putc('\n');
    console_set_colors(VGA_YELLOW, VGA_BLUE);
    console_write(" +-- CONFIRM ----------------------------------------------------------+ ");
    console_set_colors(VGA_LIGHT_GREY, VGA_BLACK);
    console_putc('\n');
    box_line(action);
    if (detail && detail[0]) box_line(detail);
    if (discarded) {
        console_set_colors(VGA_YELLOW, VGA_BLUE);
        console_write(" | discarded ");
        console_write_dec(discarded);
        console_write(" queued keystroke(s) typed before this box appeared");
        console_set_colors(VGA_LIGHT_GREY, VGA_BLACK);
        console_putc('\n');
    }
    box_line("press  y = allow   n = deny   (physical keyboard only)");

    /* 防线 2：只认 y / n，超时拒绝。等键的方式和 shell 一样：cli 检查、sti; hlt */
    start = time_ms();
    for (;;) {
        int c;
        unsigned int elapsed = time_ms() - start;
        if (elapsed >= CONFIRM_TIMEOUT_MS) break;
        left = (CONFIRM_TIMEOUT_MS - elapsed + 999) / 1000;
        if (left != last_shown) {           /* 倒计时：只在秒数变化时重画，用 \r 覆盖同一行 */
            console_set_colors(VGA_YELLOW, VGA_BLUE);
            console_write("\r | waiting ");
            console_write_dec(left);
            console_write(" s ... ");
            console_set_colors(VGA_LIGHT_GREY, VGA_BLACK);
            last_shown = left;
        }
        tel_poll();
        __asm__ __volatile__("cli");
        c = keyboard_getchar();
        if (c >= 0) {
            __asm__ __volatile__("sti");
            if (c == 'y' || c == 'Y') { answer = 1; break; }
            if (c == 'n' || c == 'N') { answer = 0; break; }
            continue;                       /* 别的键：忽略 */
        }
        __asm__ __volatile__("sti; hlt");
    }

    console_set_colors(VGA_YELLOW, VGA_BLUE);
    console_write("\r | answer: ");
    console_write(answer == 1 ? "ALLOWED by user      " : answer == 0 ? "DENIED by user       " : "TIMEOUT, treated as deny");
    console_set_colors(VGA_LIGHT_GREY, VGA_BLACK);
    console_putc('\n');
    return answer;
}
