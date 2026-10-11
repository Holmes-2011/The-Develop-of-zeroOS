#ifndef TELEMETRY_H
#define TELEMETRY_H

/* 感知层（v0.11）：让系统"看得见自己"。

   三样东西：
   1. 计数器：各个子系统发生了什么、发生了多少次（中断、键盘、堆、物理页、缺页、CPU 空闲时间）。
      计数只增不减（32 位，回绕没关系：使用方只看两次采样之间的差）。
   2. 采样：时钟中断里每 100 毫秒把所有计数器拍一张"快照"，放进环形缓冲区。
      在中断里做，所以主循环被长命令占住时（memtest、bench）也不会漏采。
   3. 事件日志：少见但重要的事（键盘缓冲区溢出、非法释放、伪中断……），带时间。
      同一种事件 1 秒内连续发生会合并成一条并计数，不会被刷屏。

   本文件属于核心代码：只用整数，可以在中断里调用。
   采样之后的处理（算速率、交给以后的系统直觉模块）在主循环里做，见 telemetry_poll()。 */

/* ---------- 采样里的各个字段 ----------
   "累计" = 开机以来一共多少（相邻两次采样相减得到这 100 毫秒里的量）
   "当前" = 采样那一刻的值 */
enum tel_field {
    TEL_IRQ_TIMER,       /* 累计：时钟中断（IRQ0）次数 */
    TEL_IRQ_KEYBOARD,    /* 累计：键盘中断（IRQ1）次数 */
    TEL_IRQ_OTHER,       /* 累计：其他 IRQ 次数 */
    TEL_IRQ_SPURIOUS,    /* 累计：伪中断（IRQ7 / IRQ15，见 pic.h）次数 */
    TEL_KBD_CHARS,       /* 累计：键盘产生的字符数 */
    TEL_KBD_DROPS,       /* 累计：键盘缓冲区满、被丢掉的字符数 */
    TEL_HEAP_ALLOCS,     /* 累计：kmalloc 成功次数 */
    TEL_HEAP_FREES,      /* 累计：kfree 成功次数 */
    TEL_HEAP_FAILS,      /* 累计：kmalloc 失败次数（内存不够） */
    TEL_HEAP_BAD_FREES,  /* 累计：重复释放 / 释放非法指针的次数 */
    TEL_HEAP_USED,       /* 当前：堆里已分配出去的字节数 */
    TEL_HEAP_MAPPED,     /* 当前：堆一共映射了多少字节 */
    TEL_PAGES_FREE,      /* 当前：空闲物理页数 */
    TEL_PAGE_ALLOCS,     /* 累计：物理页分配次数 */
    TEL_PAGE_FREES,      /* 累计：物理页释放次数 */
    TEL_PAGE_BAD_FREES,  /* 累计：物理页重复释放 / 非法释放次数 */
    TEL_DEMAND_FAULTS,   /* 累计：按需分页区里处理掉的缺页次数（见 demand.h） */
    TEL_IDLE_US,         /* 累计：CPU 空闲（hlt 里）的微秒数；没有 TSC 时恒为 0 */
    TEL_NFIELDS
};

struct tel_sample {
    unsigned int seq;            /* 第几次采样（从 0 开始，连续） */
    unsigned int t_ms;           /* 采样时刻，开机以来的毫秒数 */
    unsigned int v[TEL_NFIELDS];
};

#define TEL_PERIOD_MS 100        /* 采样间隔 */
#define TEL_RING      256        /* 环形缓冲区能存多少次采样（约 25 秒） */

/* ---------- 计数（各子系统调用；中断里也可以调用） ---------- */
void tel_irq(unsigned char irq);        /* isr_handler：每次硬件中断 */
void tel_spurious(unsigned char irq);   /* isr_handler：伪中断 */
void tel_kbd_char(void);
void tel_kbd_drop(void);
void tel_heap_alloc(unsigned int bytes);
void tel_heap_free(unsigned int bytes);
void tel_heap_fail(unsigned int bytes);
void tel_heap_bad_free(unsigned int addr);
void tel_page_alloc(void);
void tel_page_free(void);
void tel_page_bad_free(unsigned int addr);
void tel_demand_fault(unsigned int addr);

/* CPU 空闲计时：主循环在 hlt 前后各调一次 */
void tel_idle_begin(void);
void tel_idle_end(void);

/* ---------- 采样 ---------- */
void tel_init(void);
void tel_timer_tick(void);   /* 时钟中断里调用：到了 100 毫秒就拍一次快照 */

/* 取第 seq 次采样；已经被覆盖（太旧）或还没发生返回 0 */
int  tel_get_sample(unsigned int seq, struct tel_sample *out);
unsigned int tel_latest_seq(void);       /* 最新一次采样的序号；一次都没采过时返回 0xFFFFFFFF */
void tel_read_now(struct tel_sample *out);   /* 不等采样，直接读当前所有计数器 */
const char *tel_field_name(int field);

/* ---------- 主循环 ---------- */
void tel_poll(void);                     /* 主循环每次醒来调用：处理新采样（目前是串口实时输出） */
void tel_stream(int on);                 /* 每次采样往串口输出一行 T,...（采集训练数据用） */

/* ---------- 事件日志 ---------- */
enum tel_event {
    EV_BOOT,             /* 开机完成 */
    EV_KBD_OVERFLOW,     /* 键盘缓冲区满 */
    EV_SPURIOUS_IRQ,     /* 伪中断；a = IRQ 号 */
    EV_HEAP_FAIL,        /* kmalloc 失败；a = 申请的字节数 */
    EV_HEAP_BAD_FREE,    /* 重复释放 / 非法指针；a = 地址 */
    EV_PAGE_BAD_FREE,    /* 物理页重复释放 / 非法释放；a = 地址 */
    EV_DEMAND_FAULT,     /* 按需分页区缺页；a = 地址 */
    EV_DEMAND_OOM,       /* 按需分页区缺页但没有空闲物理页；a = 地址 */
    EV_SAMPLES_LOST,     /* 主循环太久没处理，旧采样被覆盖；a = 丢了几次 */
    EV_NTYPES
};

struct tel_event_rec {
    unsigned int t_ms;    /* 最后一次发生的时间 */
    unsigned int type;
    unsigned int count;   /* 1 秒内的同类事件合并在一条里 */
    unsigned int a;       /* 最后一次的参数 */
};

#define TEL_EVENTS 64

void tel_event(enum tel_event type, unsigned int a);
int  tel_get_events(struct tel_event_rec *out, int max);   /* 从旧到新，返回条数 */
unsigned int tel_event_total(void);                        /* 开机以来一共记录了几条（含被覆盖的） */
const char *tel_event_name(unsigned int type);

#endif
