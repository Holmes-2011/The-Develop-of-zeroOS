#include "telemetry.h"
#include "time.h"
#include "pmm.h"
#include "heap.h"
#include "serial.h"

/* ---------------- 计数器 ----------------
   中断和主循环都会加计数，用原子加（x86 上是一条带 lock 的指令），
   避免"主循环读出旧值、中断插进来加了 1、主循环写回"把那次加 1 弄丢 */
static volatile unsigned int ctr[TEL_NFIELDS];

static void add(int field, unsigned int n) { __atomic_fetch_add(&ctr[field], n, __ATOMIC_RELAXED); }

/* 保存中断状态并关中断 / 恢复：给"中断和主循环都会写"的那几处用 */
static unsigned int irq_save(void) {
    unsigned int flags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void irq_restore(unsigned int flags) {
    if (flags & 0x200) __asm__ __volatile__("sti" : : : "memory");   /* 第 9 位 IF：原来是开着的才打开 */
}

void tel_irq(unsigned char irq) {
    add(irq == 0 ? TEL_IRQ_TIMER : irq == 1 ? TEL_IRQ_KEYBOARD : TEL_IRQ_OTHER, 1);
}
void tel_spurious(unsigned char irq)    { add(TEL_IRQ_SPURIOUS, 1); tel_event(EV_SPURIOUS_IRQ, irq); }
void tel_kbd_char(void)                 { add(TEL_KBD_CHARS, 1); }
void tel_kbd_drop(void)                 { add(TEL_KBD_DROPS, 1); tel_event(EV_KBD_OVERFLOW, 0); }
void tel_heap_alloc(unsigned int bytes) { add(TEL_HEAP_ALLOCS, 1); add(TEL_HEAP_USED, bytes); }
void tel_heap_free(unsigned int bytes)  { add(TEL_HEAP_FREES, 1); add(TEL_HEAP_USED, 0U - bytes); }
void tel_heap_fail(unsigned int bytes)  { add(TEL_HEAP_FAILS, 1); tel_event(EV_HEAP_FAIL, bytes); }
void tel_heap_bad_free(unsigned int a)  { add(TEL_HEAP_BAD_FREES, 1); tel_event(EV_HEAP_BAD_FREE, a); }
void tel_page_alloc(void)               { add(TEL_PAGE_ALLOCS, 1); }
void tel_page_free(void)                { add(TEL_PAGE_FREES, 1); }
void tel_page_bad_free(unsigned int a)  { add(TEL_PAGE_BAD_FREES, 1); tel_event(EV_PAGE_BAD_FREE, a); }
void tel_demand_fault(unsigned int a)   { add(TEL_DEMAND_FAULTS, 1); tel_event(EV_DEMAND_FAULT, a); }

/* ---------------- CPU 空闲时间 ----------------
   主循环没事做时执行 hlt 睡觉，直到下一个中断。hlt 前后各读一次 TSC，累加起来就是空闲时间；
   忙碌比例 = 1 - 空闲时间 / 总时间。长命令（memtest、bench）期间主循环不睡，自然算作忙 */
static unsigned long long idle_tsc;     /* 累计空闲的 TSC 计数 */
static unsigned long long idle_start;
static volatile int in_idle;

void tel_idle_begin(void) {             /* 调用时中断是关着的（见 shell.c 的 wait_char） */
    if (!time_tsc_khz()) return;
    idle_start = time_tsc();
    in_idle = 1;
}

void tel_idle_end(void) {
    unsigned int flags;
    if (!in_idle) return;
    flags = irq_save();                 /* 64 位累加不是一条指令，不能被采样中断打断一半 */
    idle_tsc += time_tsc() - idle_start;
    in_idle = 0;
    irq_restore(flags);
}

/* 到此刻为止的累计空闲微秒数。正在 hlt 里被采样中断叫醒时，把这一段也算进去，保证只增不减 */
static unsigned int idle_us_now(void) {
    unsigned long long t = idle_tsc;
    if (in_idle) t += time_tsc() - idle_start;
    return time_tsc_to_us(t);
}

/* ---------------- 采样 ---------------- */
static struct tel_sample ring[TEL_RING];
static volatile unsigned int taken;     /* 一共采了几次；第 k 次在 ring[k % TEL_RING] */
static unsigned int next_ms;
static int sampling;

void tel_read_now(struct tel_sample *s) {
    int i;
    s->seq = taken;
    s->t_ms = time_ms();
    for (i = 0; i < TEL_NFIELDS; i++) s->v[i] = ctr[i];
    s->v[TEL_HEAP_MAPPED] = heap_mapped_bytes();
    s->v[TEL_PAGES_FREE] = pmm_free_pages();
    s->v[TEL_IDLE_US] = idle_us_now();
}

void tel_init(void) {
    next_ms = time_ms() + TEL_PERIOD_MS;
    sampling = 1;
}

/* 在时钟中断里调用。按时间（而不是按中断次数）采样：
   以后注入"中断风暴"时时钟中断会变快，按次数采样间隔就不准了 */
void tel_timer_tick(void) {
    unsigned int now;
    if (!sampling) return;
    now = time_ms();
    if ((int) (now - next_ms) < 0) return;
    tel_read_now(&ring[taken % TEL_RING]);
    ring[taken % TEL_RING].t_ms = now;
    taken++;
    next_ms += TEL_PERIOD_MS;
    if ((int) (now - next_ms) >= 0) next_ms = now + TEL_PERIOD_MS;   /* 落后太多（关中断太久）：不补采 */
}

int tel_get_sample(unsigned int seq, struct tel_sample *out) {
    unsigned int flags = irq_save();
    int ok = seq < taken && taken - seq <= TEL_RING;
    if (ok) *out = ring[seq % TEL_RING];
    irq_restore(flags);
    return ok;
}

unsigned int tel_latest_seq(void) { return taken ? taken - 1 : 0xFFFFFFFF; }

const char *tel_field_name(int f) {
    static const char *names[TEL_NFIELDS] = {
        "irq_timer", "irq_keyboard", "irq_other", "irq_spurious", "kbd_chars", "kbd_drops",
        "heap_allocs", "heap_frees", "heap_fails", "heap_bad_frees", "heap_used", "heap_mapped",
        "pages_free", "page_allocs", "page_frees", "page_bad_frees", "demand_faults", "idle_us"
    };
    return f >= 0 && f < TEL_NFIELDS ? names[f] : "?";
}

/* ---------------- 主循环：处理新采样 ---------------- */
static unsigned int polled;     /* 下一个要处理的采样序号 */
static int streaming;

static void serial_dec(unsigned int n) {
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    do { buf[--i] = (char) ('0' + n % 10); n /= 10; } while (n);
    serial_write_string(&buf[i]);
}

void tel_stream(int on) {
    int i;
    streaming = on;
    if (!on) return;
    polled = taken;   /* 从现在开始输出，不补以前的 */
    /* 表头：字段名，方便采集脚本按名字取列，以后加字段也不会对错列 */
    serial_write_string("TH,seq,t_ms");
    for (i = 0; i < TEL_NFIELDS; i++) {
        serial_write_string(",");
        serial_write_string(tel_field_name(i));
    }
    serial_write_string("\n");
}

void tel_poll(void) {
    struct tel_sample s;
    unsigned int latest = taken;
    if (latest - polled > TEL_RING) {               /* 主循环被占太久，最旧的已经被覆盖 */
        tel_event(EV_SAMPLES_LOST, latest - polled - TEL_RING);
        polled = latest - TEL_RING;
    }
    while (polled != latest) {
        if (!tel_get_sample(polled, &s)) { polled++; continue; }
        polled++;
        if (streaming) {
            int i;
            serial_write_string("T,");
            serial_dec(s.seq);
            serial_write_string(",");
            serial_dec(s.t_ms);
            for (i = 0; i < TEL_NFIELDS; i++) {
                serial_write_string(",");
                serial_dec(s.v[i]);
            }
            serial_write_string("\n");
        }
        /* v0.13：这里把采样交给系统直觉模块 */
    }
}

/* ---------------- 事件日志 ---------------- */
static struct tel_event_rec events[TEL_EVENTS];
static unsigned int ev_total;

void tel_event(enum tel_event type, unsigned int a) {
    unsigned int flags = irq_save();   /* 中断（键盘溢出、伪中断）和主循环都会写 */
    unsigned int now = time_ms();
    struct tel_event_rec *last = ev_total ? &events[(ev_total - 1) % TEL_EVENTS] : 0;
    if (last && last->type == (unsigned int) type && now - last->t_ms < 1000) {
        last->count++;                 /* 同类事件 1 秒内接连发生：合并，不刷屏 */
        last->a = a;
        last->t_ms = now;
    } else {
        struct tel_event_rec *e = &events[ev_total % TEL_EVENTS];
        e->t_ms = now;
        e->type = (unsigned int) type;
        e->count = 1;
        e->a = a;
        ev_total++;
    }
    irq_restore(flags);
}

int tel_get_events(struct tel_event_rec *out, int max) {
    unsigned int flags = irq_save();
    unsigned int n = ev_total < TEL_EVENTS ? ev_total : TEL_EVENTS, i;
    if (n > (unsigned int) max) n = (unsigned int) max;
    for (i = 0; i < n; i++) out[i] = events[(ev_total - n + i) % TEL_EVENTS];
    irq_restore(flags);
    return (int) n;
}

unsigned int tel_event_total(void) { return ev_total; }

const char *tel_event_name(unsigned int type) {
    static const char *names[EV_NTYPES] = {
        "boot", "keyboard buffer overflow", "spurious irq", "heap allocation failed",
        "heap bad free", "page bad free", "demand fault", "demand fault, out of memory", "samples lost"
    };
    return type < EV_NTYPES ? names[type] : "?";
}
