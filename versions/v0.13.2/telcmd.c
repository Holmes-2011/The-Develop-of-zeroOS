/* 感知层的命令行命令（v0.12）：metrics / events / telstream。核心代码，只用整数。 */

#include "telcmd.h"
#include "telemetry.h"
#include "demand.h"
#include "console.h"
#include "time.h"

/* 速率：最新一次采样和大约 1 秒前那次采样之间，每秒多少 */
static unsigned int rate(const struct tel_sample *a, const struct tel_sample *b, int f) {
    unsigned int dt = b->t_ms - a->t_ms;
    if (dt == 0) return 0;
    return (unsigned int) div64_32((unsigned long long) (b->v[f] - a->v[f]) * 1000, dt);
}

static void count_rate(const char *name, const struct tel_sample *a, const struct tel_sample *b, int f) {
    console_write(name);
    console_putc(' ');
    console_write_dec(b->v[f]);
    console_write(" (");
    console_write_dec(rate(a, b, f));
    console_write("/s)");
}

static void ms_as_seconds(unsigned int ms) {   /* 右对齐的 "123.456 s" */
    unsigned int s = ms / 1000, w = 1, x = s;
    while (x >= 10) { x /= 10; w++; }
    while (w++ < 6) console_putc(' ');
    console_write_dec(s);
    console_putc('.');
    console_putc((char) ('0' + ms % 1000 / 100));
    console_putc((char) ('0' + ms % 100 / 10));
    console_putc((char) ('0' + ms % 10));
    console_write(" s");
}

void cmd_metrics(const char *args) {
    struct tel_sample a, b;
    unsigned int latest = tel_latest_seq(), back;
    (void) args;
    if (latest == 0xFFFFFFFF || latest < 1) {
        console_write("metrics: no samples yet, try again in a moment\n");
        return;
    }
    back = latest >= 10 ? latest - 10 : 0;          /* 约 1 秒前 */
    if (!tel_get_sample(back, &a) || !tel_get_sample(latest, &b)) {
        console_write("metrics: samples unavailable\n");
        return;
    }

    console_write("metrics (last ");
    console_write_dec((b.t_ms - a.t_ms) / 1000);
    console_putc('.');
    console_putc((char) ('0' + (b.t_ms - a.t_ms) % 1000 / 100));
    console_write(" s; ");
    console_write_dec(latest + 1);
    console_write(" samples since boot, every ");
    console_write_dec(TEL_PERIOD_MS);
    console_write(" ms):\n");

    console_write("  irq      ");
    count_rate("timer", &a, &b, TEL_IRQ_TIMER);
    console_write(", ");
    count_rate("keyboard", &a, &b, TEL_IRQ_KEYBOARD);
    console_write(", other ");
    console_write_dec(b.v[TEL_IRQ_OTHER]);
    console_write(", spurious ");
    console_write_dec(b.v[TEL_IRQ_SPURIOUS]);
    console_putc('\n');

    console_write("  keyboard chars ");
    console_write_dec(b.v[TEL_KBD_CHARS]);
    console_write(", dropped ");
    console_write_dec(b.v[TEL_KBD_DROPS]);
    console_putc('\n');

    console_write("  heap     used ");
    console_write_dec(b.v[TEL_HEAP_USED]);
    console_write(" B, mapped ");
    console_write_dec(b.v[TEL_HEAP_MAPPED] / 1024);
    console_write(" KB\n  heap     ");
    count_rate("allocs", &a, &b, TEL_HEAP_ALLOCS);
    console_write(", ");
    count_rate("frees", &a, &b, TEL_HEAP_FREES);
    console_write(", failed ");
    console_write_dec(b.v[TEL_HEAP_FAILS]);
    console_write(", bad frees ");
    console_write_dec(b.v[TEL_HEAP_BAD_FREES]);
    console_putc('\n');

    console_write("  pages    free ");
    console_write_dec(b.v[TEL_PAGES_FREE]);
    console_write(", ");
    count_rate("allocs", &a, &b, TEL_PAGE_ALLOCS);
    console_write(", ");
    count_rate("frees", &a, &b, TEL_PAGE_FREES);
    console_write(", bad frees ");
    console_write_dec(b.v[TEL_PAGE_BAD_FREES]);
    console_putc('\n');

    console_write("  demand   ");
    count_rate("faults", &a, &b, TEL_DEMAND_FAULTS);
    console_write(", mapped ");
    console_write_dec(demand_pages());
    console_write(" pages\n");

    console_write("  cpu      busy ");
    if (time_tsc_khz()) {
        unsigned int dt_us = (b.t_ms - a.t_ms) * 1000, idle = b.v[TEL_IDLE_US] - a.v[TEL_IDLE_US];
        console_write_dec(idle >= dt_us ? 0 : 100 - (unsigned int) div64_32((unsigned long long) idle * 100, dt_us));
        console_write("%\n");
    } else {
        console_write("n/a (no TSC)\n");
    }
}

void cmd_events(const char *args) {
    struct tel_event_rec ev[TEL_EVENTS];
    int n = tel_get_events(ev, TEL_EVENTS), i;
    (void) args;
    console_write("events: ");
    console_write_dec(tel_event_total());
    console_write(" since boot (showing last ");
    console_write_dec((unsigned int) n);
    console_write(")\n");
    for (i = 0; i < n; i++) {
        console_write("  ");
        ms_as_seconds(ev[i].t_ms);
        console_write("  ");
        console_write(tel_event_name(ev[i].type));
        if (ev[i].count > 1) {
            console_write(" x");
            console_write_dec(ev[i].count);
        }
        if (ev[i].type != EV_BOOT) {
            console_write(" (");
            if (ev[i].type == EV_SAMPLES_LOST || ev[i].type == EV_SPURIOUS_IRQ || ev[i].type == EV_HEAP_FAIL) {
                console_write_dec(ev[i].a);
            } else {
                console_write_hex(ev[i].a);
            }
            console_putc(')');
        }
        console_putc('\n');
    }
}

static int word_is(const char *s, const char *w) {
    while (*w && *s == *w) { s++; w++; }
    return *w == '\0' && (*s == '\0' || *s == ' ');
}

void cmd_telstream(const char *args) {
    if (word_is(args, "on")) {
        tel_stream(1);
        console_write("telemetry stream on: one line per sample on COM1 (serial only)\n");
    } else if (word_is(args, "off")) {
        tel_stream(0);
        console_write("telemetry stream off\n");
    } else {
        console_write("usage: telstream on|off\n");
    }
}
