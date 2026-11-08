/* 内置工具（v0.13）：第一批登记进工具层的工具。核心代码，只用整数。

   查询类（只读）   get_uptime get_memory get_cpu get_metrics get_events list_disks
   诊断类（只读）   heap_check pmm_check
   可撤销          set_telstream
   破坏性          clear_events
   系统级          inject_fault（只在开发者模式下可用，而且要确认）——给系统直觉模块制造训练素材 */

#include "tools.h"
#include "time.h"
#include "pit.h"
#include "pmm.h"
#include "heap.h"
#include "demand.h"
#include "cpu.h"
#include "telemetry.h"
#include "blockdev.h"
#include "kstring.h"
#include "stack.h"
#include "bridge.h"

static int word_is(const char *args, const char *w) {
    unsigned int n = strlen(w);
    return memcmp(args, w, n) == 0 && (args[n] == '\0' || args[n] == ' ');
}

static int parse_uint(const char *s, unsigned int *v) {
    *v = 0;
    if (!*s) return -1;
    for (; *s && *s != ' '; s++) {
        if (*s < '0' || *s > '9') return -1;
        *v = *v * 10 + (unsigned int) (*s - '0');
    }
    return 0;
}

/* ---------------- 查询 ---------------- */

static int t_get_uptime(const char *args, char *out, unsigned int cap) {
    struct tool_out o;
    (void) args;
    tout_init(&o, out, cap);
    tout_str(&o, "uptime_ms=");
    tout_dec(&o, time_ms());
    tout_str(&o, " clock=");
    tout_str(&o, time_source());
    tout_str(&o, " timer_irqs=");
    tout_dec(&o, pit_ticks());
    return 0;
}

static int t_get_memory(const char *args, char *out, unsigned int cap) {
    struct tool_out o;
    struct heap_stats hs;
    (void) args;
    heap_get_stats(&hs);
    tout_init(&o, out, cap);
    tout_str(&o, "free_pages=");    tout_dec(&o, pmm_free_pages());
    tout_str(&o, " usable_pages="); tout_dec(&o, pmm_total_pages());
    tout_str(&o, " free_kb=");      tout_dec(&o, pmm_free_pages() * 4);
    tout_str(&o, " heap_used=");    tout_dec(&o, hs.used);
    tout_str(&o, " heap_mapped=");  tout_dec(&o, hs.mapped);
    tout_str(&o, " heap_blocks=");  tout_dec(&o, hs.blocks);
    tout_str(&o, " demand_pages="); tout_dec(&o, demand_pages());
    return 0;
}

static int t_get_cpu(const char *args, char *out, unsigned int cap) {
    const struct cpu_info *c = cpu_get_info();
    struct tool_out o;
    (void) args;
    tout_init(&o, out, cap);
    tout_str(&o, "vendor=");  tout_str(&o, c->vendor);
    tout_str(&o, " float=");  tout_str(&o, c->float_enabled ? "sse2" : "off");
    tout_str(&o, " tsc_khz=");tout_dec(&o, time_tsc_khz());
    tout_str(&o, " model=\""); tout_str(&o, c->brand[0] ? c->brand : "?"); tout_str(&o, "\"");
    return 0;
}

/* 最近 1 秒的速率和 CPU 忙碌比例：和 metrics 命令同一个算法 */
static int t_get_metrics(const char *args, char *out, unsigned int cap) {
    struct tel_sample a, b;
    unsigned int latest = tel_latest_seq(), back, dt, idle, busy_pct;
    struct tool_out o;
    (void) args;
    tout_init(&o, out, cap);
    if (latest == 0xFFFFFFFF || latest < 1) { tout_str(&o, "no samples yet"); return -1; }
    back = latest >= 10 ? latest - 10 : 0;
    if (!tel_get_sample(back, &a) || !tel_get_sample(latest, &b)) { tout_str(&o, "samples unavailable"); return -1; }
    dt = b.t_ms - a.t_ms;
    if (dt == 0) dt = 1;
    idle = (b.v[TEL_IDLE_US] - a.v[TEL_IDLE_US]) / 1000;      /* 毫秒 */
    busy_pct = idle >= dt ? 0 : (dt - idle) * 100 / dt;
    tout_str(&o, "window_ms=");      tout_dec(&o, dt);
    tout_str(&o, " cpu_busy_pct=");  tout_dec(&o, busy_pct);
    tout_str(&o, " timer_irq_ps=");  tout_dec(&o, (b.v[TEL_IRQ_TIMER] - a.v[TEL_IRQ_TIMER]) * 1000 / dt);
    tout_str(&o, " kbd_chars=");     tout_dec(&o, b.v[TEL_KBD_CHARS] - a.v[TEL_KBD_CHARS]);
    tout_str(&o, " heap_allocs=");   tout_dec(&o, b.v[TEL_HEAP_ALLOCS] - a.v[TEL_HEAP_ALLOCS]);
    tout_str(&o, " heap_used=");     tout_dec(&o, b.v[TEL_HEAP_USED]);
    tout_str(&o, " free_pages=");    tout_dec(&o, b.v[TEL_PAGES_FREE]);
    tout_str(&o, " demand_faults="); tout_dec(&o, b.v[TEL_DEMAND_FAULTS]);
    tout_str(&o, " bad_frees=");     tout_dec(&o, b.v[TEL_HEAP_BAD_FREES] + b.v[TEL_PAGE_BAD_FREES]);
    return 0;
}

static int t_get_events(const char *args, char *out, unsigned int cap) {
    static struct tel_event_rec ev[8];
    int n = tel_get_events(ev, 8), i;
    struct tool_out o;
    (void) args;
    tout_init(&o, out, cap);
    tout_str(&o, "total=");
    tout_dec(&o, tel_event_total());
    for (i = 0; i < n; i++) {
        tout_str(&o, "; t=");    tout_dec(&o, ev[i].t_ms);
        tout_str(&o, " ");       tout_str(&o, tel_event_name(ev[i].type));
        if (ev[i].count > 1) { tout_str(&o, " x"); tout_dec(&o, ev[i].count); }
        if (ev[i].a) { tout_str(&o, " a="); tout_hex(&o, ev[i].a); }
    }
    return 0;
}

static int t_list_disks(const char *args, char *out, unsigned int cap) {
    struct tool_out o;
    int i;
    (void) args;
    tout_init(&o, out, cap);
    tout_str(&o, "count=");
    tout_dec(&o, (unsigned int) blockdev_count());
    for (i = 0; i < blockdev_count(); i++) {
        struct block_device *d = blockdev_get(i);
        tout_str(&o, "; ");       tout_str(&o, d->name);
        tout_str(&o, " sectors="); tout_dec(&o, (unsigned int) d->sectors);
        tout_str(&o, " mb=");     tout_dec(&o, (unsigned int) (d->sectors >> 11));
        tout_str(&o, " lba48=");  tout_dec(&o, (unsigned int) d->lba48);
        tout_str(&o, " model=\""); tout_str(&o, d->model); tout_str(&o, "\"");
    }
    return 0;
}

/* ---------------- 诊断 ---------------- */

static int t_heap_check(const char *args, char *out, unsigned int cap) {
    unsigned int blocks, bad;
    struct tool_out o;
    int r = heap_check(&blocks, &bad);
    (void) args;
    tout_init(&o, out, cap);
    if (r == 0) { tout_str(&o, "ok blocks="); tout_dec(&o, blocks); return 0; }
    tout_str(&o, "CORRUPT code="); tout_dec(&o, (unsigned int) -r);
    tout_str(&o, " at=");          tout_hex(&o, bad);
    tout_str(&o, " blocks_walked="); tout_dec(&o, blocks);
    return -1;
}

static int t_pmm_check(const char *args, char *out, unsigned int cap) {
    unsigned int counted, claimed;
    struct tool_out o;
    int r = pmm_check(&counted, &claimed);
    (void) args;
    tout_init(&o, out, cap);
    tout_str(&o, r == 0 ? "ok" : "MISMATCH");
    tout_str(&o, " bitmap_free="); tout_dec(&o, counted);
    tout_str(&o, " counter_free="); tout_dec(&o, claimed);
    return r == 0 ? 0 : -1;
}

static int t_stack_check(const char *args, char *out, unsigned int cap) {
    struct tool_out o;
    (void) args;
    tout_init(&o, out, cap);
    tout_str(&o, stack_overflowed() ? "OVERFLOW" : "ok");
    tout_str(&o, " size=");       tout_dec(&o, stack_size());
    tout_str(&o, " high_water="); tout_dec(&o, stack_high_water());
    tout_str(&o, " free_min=");   tout_dec(&o, stack_size() - stack_high_water());
    return stack_overflowed() ? -1 : 0;
}

static int t_get_intuition(const char *args, char *out, unsigned int cap) {
    const struct intuition *v = bridge_last_verdict();
    struct tool_out o;
    (void) args;
    tout_init(&o, out, cap);
    if (!v->count) { tout_str(&o, "none (no verdict received yet)"); return 0; }
    tout_str(&o, "class=");       tout_str(&o, v->cls);
    tout_str(&o, " confidence="); tout_dec(&o, v->confidence);
    tout_str(&o, " t_ms=");       tout_dec(&o, v->t_ms);
    tout_str(&o, " count=");      tout_dec(&o, v->count);
    if (v->note[0]) { tout_str(&o, " note=\""); tout_str(&o, v->note); tout_str(&o, "\""); }
    return 0;
}

/* ---------------- 可撤销 / 破坏性 ---------------- */

static int t_set_telstream(const char *args, char *out, unsigned int cap) {
    struct tool_out o;
    tout_init(&o, out, cap);
    if (word_is(args, "on"))       { tel_stream(1); tout_str(&o, "telstream=on"); return 0; }
    else if (word_is(args, "off")) { tel_stream(0); tout_str(&o, "telstream=off"); return 0; }
    return -2;
}

static int t_clear_events(const char *args, char *out, unsigned int cap) {
    struct tool_out o;
    (void) args;
    tout_init(&o, out, cap);
    tout_str(&o, "cleared=");
    tout_dec(&o, tel_clear_events());
    return 0;
}

/* ---------------- 系统级：故障注入 ---------------- */

/* 把栈指针往下压 kb KB、在最深处写一个字节、再恢复：stack_check 的水位应该升到这个深度。
   （第一版用递归 + volatile 局部数组，clang 发现数组只用了两个元素，把它缩成了 2 字节——
   栈根本没被用掉。直接操作 esp 没有这个问题） */
static __attribute__((noinline)) void burn_stack(unsigned int kb) {
    __asm__ __volatile__(
        "mov %%esp, %%eax\n\t"
        "sub %0, %%esp\n\t"
        "movb $1, (%%esp)\n\t"
        "mov %%eax, %%esp"
        : : "r"(kb * 1024) : "eax", "memory");
}

static void *leaked[16];
static int nleaked;
static unsigned int touched;   /* demand_touch 用过几页：每次碰一页新的，不和别的使用者撞 */

static int t_inject_fault(const char *args, char *out, unsigned int cap) {
    struct tool_out o;
    tout_init(&o, out, cap);

    if (word_is(args, "heap_leak")) {
        /* 申请 64KB 不释放：heap_used 升高、再也不降，直觉模块应该学会认出"泄漏" */
        void *p = kmalloc(64 * 1024);
        if (!p) { tout_str(&o, "kmalloc failed"); return -1; }
        if (nleaked < 16) leaked[nleaked++] = p;
        tout_str(&o, "leaked 65536 bytes (total leaks ");
        tout_dec(&o, (unsigned int) nleaked);
        tout_str(&o, ")");
        return 0;
    }
    if (word_is(args, "bad_free")) {
        /* 释放一个不是 kmalloc 给的指针：堆会拒绝并计数（heap_bad_frees + 事件），不会真的坏 */
        kfree((void *) (HEAP_START + 0x12340));
        tout_str(&o, "freed an invalid pointer (heap should have rejected it)");
        return 0;
    }
    if (word_is(args, "demand_touch")) {
        /* 碰按需分页区里的一页：触发一次可观测的缺页 */
        volatile unsigned int *p = (volatile unsigned int *) (DEMAND_START + 0x2000000 + (touched++ << 12));
        *p = 0x5A0A5A0A;   /* 从区域的 32MB 处开始，memtest 等不会用到那里 */
        tout_str(&o, "touched demand page, demand_pages=");
        tout_dec(&o, demand_pages());
        return 0;
    }
    if (word_is(args, "stack_use")) {
        /* 用掉 N KB 栈（最多 12KB，留 4KB 余量不真的溢出）：训练"栈快满了"这种信号 */
        unsigned int kb;
        if (args[9] != ' ' || parse_uint(args + 10, &kb) < 0 || kb == 0 || kb > 12) return -2;
        burn_stack(kb);
        tout_str(&o, "used ");
        tout_dec(&o, kb);
        tout_str(&o, " KB of stack, high_water now ");
        tout_dec(&o, stack_high_water());
        return 0;
    }
    if (word_is(args, "spin")) {
        /* 空转若干毫秒：CPU 忙碌比例飙到 100%，时钟中断照常 */
        unsigned int ms, t0;
        /* "spin" 后面必须有空格和数字；args 只有 "spin" 时 args[4] 是结尾的 0，不能再往后读 */
        if (args[4] != ' ' || parse_uint(args + 5, &ms) < 0 || ms == 0 || ms > 10000) return -2;
        t0 = time_ms();
        while (time_ms() - t0 < ms) { }
        tout_str(&o, "spun for ");
        tout_dec(&o, ms);
        tout_str(&o, " ms");
        return 0;
    }
    return -2;
}

/* ---------------- 登记 ---------------- */

static const struct tool builtin[] = {
    { "get_uptime",   "time since boot and clock source",                  "",            TOOL_READONLY,    t_get_uptime },
    { "get_memory",   "free pages, heap usage, demand-paged pages",        "",            TOOL_READONLY,    t_get_memory },
    { "get_cpu",      "processor vendor, model, float and clock",          "",            TOOL_READONLY,    t_get_cpu },
    { "get_metrics",  "rates over the last second and CPU busy percent",   "",            TOOL_READONLY,    t_get_metrics },
    { "get_events",   "recent notable events",                             "",            TOOL_READONLY,    t_get_events },
    { "list_disks",   "block devices and their sizes",                     "",            TOOL_READONLY,    t_list_disks },
    { "heap_check",   "walk the kernel heap and verify every block",       "",            TOOL_READONLY,    t_heap_check },
    { "pmm_check",    "recount free pages in the bitmap vs the counter",   "",            TOOL_READONLY,    t_pmm_check },
    { "stack_check",  "kernel stack size, high-water mark, overflow guard", "",           TOOL_READONLY,    t_stack_check },
    { "get_intuition","last verdict submitted by the host-side intuition model", "",      TOOL_READONLY,    t_get_intuition },
    { "set_telstream","start/stop the telemetry stream on COM1",           "on|off",      TOOL_REVERSIBLE,  t_set_telstream },
    { "clear_events", "erase the event log",                               "",            TOOL_DESTRUCTIVE, t_clear_events },
    { "inject_fault", "deliberately cause a fault to train the system intuition", "heap_leak|bad_free|demand_touch|spin <ms>|stack_use <kb>", TOOL_SYSTEM, t_inject_fault },
};

void tools_init(void) {
    unsigned int i;
    for (i = 0; i < sizeof(builtin) / sizeof(builtin[0]); i++) tools_register(&builtin[i]);
}
