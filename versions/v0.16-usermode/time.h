#ifndef TIME_H
#define TIME_H

/* 开机以来过了多久（v0.10.1）。

   以前直接数时钟中断（PIT 每 10 毫秒一次）。问题：CPU 满负荷时，模拟器（以及关中断太久的真机）
   会丢中断，丢一个就少算 10 毫秒——v0.10 实测满负荷时慢 0.5% ~ 1.2%。

   现在优先用 CPU 的时间戳计数器（TSC）：一个随时间不停往上数的 64 位计数器，不依赖中断。
   开机时用 PIT 的通道 2 量 50 毫秒（轮询，不用中断），算出 TSC 每毫秒走多少；
   之后 uptime = (现在的 TSC - 开机时的 TSC) ÷ 每毫秒的计数。
   没有 TSC 的 CPU（486）退回到数中断。 */

void         time_init(void);          /* 在开中断之前调用 */
unsigned int time_ms(void);            /* 开机以来的毫秒数（约 49.7 天后回绕） */
const char  *time_source(void);        /* "tsc" 或 "pit"（数中断） */
unsigned int time_tsc_khz(void);       /* TSC 频率，kHz；没用 TSC 时为 0 */

/* v0.11（感知层用）：原始 TSC 读数，以及把一段 TSC 计数换算成微秒（结果按 32 位回绕，只用来算差值）。
   没用 TSC 时换算结果恒为 0 */
unsigned long long time_tsc(void);

/* 64 位 ÷ 32 位。核心代码里不能直接写 64 位除法：编译器会调用 __udivdi3，而裸机内核没有这个库函数 */
unsigned long long div64_32(unsigned long long n, unsigned int d);
unsigned int       time_tsc_to_us(unsigned long long ticks);

#endif
