#include "time.h"
#include "cpu.h"
#include "pit.h"
#include "io.h"

#define CALIBRATE_MS 50
#define PIT_HZ       1193182U

static unsigned long long tsc_start;
static unsigned int tsc_per_ms;      /* 0 = 不用 TSC */

static unsigned long long rdtsc(void) {
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long) hi << 32) | lo;
}

/* 64 位 ÷ 32 位。核心代码不能调用编译器的 __udivdi3（裸机没有这个库），
   所以分两步用 CPU 的 divl：先除高 32 位，余数和低 32 位拼起来再除——
   第二步的商一定小于 2^32，divl 不会溢出 */
unsigned long long div64_32(unsigned long long n, unsigned int d) {
    unsigned int hi = (unsigned int) (n >> 32), lo = (unsigned int) n;
    unsigned int q_hi = hi / d, r = hi % d, q_lo;
    __asm__("divl %2" : "=a"(q_lo), "+d"(r) : "rm"(d), "0"(lo));
    return ((unsigned long long) q_hi << 32) | q_lo;
}

/* 用 PIT 通道 2 量出 CALIBRATE_MS 毫秒里 TSC 走了多少。
   通道 2 本来接 PC 喇叭；端口 0x61 的第 0 位是它的门控，第 1 位是喇叭开关，
   第 5 位是它的输出。方式 0：计数减到 0 时输出变高——轮询这一位，不需要中断 */
static unsigned long long measure_tsc(void) {
    unsigned int count = PIT_HZ / 1000 * CALIBRATE_MS;
    unsigned long long t0, t1;
    unsigned int spins = 0;

    outb(0x61, (unsigned char) ((inb(0x61) & ~0x02) | 0x01));   /* 门控开、喇叭关 */
    outb(0x43, 0xB0);                                           /* 通道 2，低字节+高字节，方式 0 */
    outb(0x42, (unsigned char) (count & 0xFF));
    outb(0x42, (unsigned char) (count >> 8));                   /* 写完高字节开始倒数 */
    t0 = rdtsc();
    while (!(inb(0x61) & 0x20)) {
        if (++spins > 100000000U) return 0;                     /* 等不到：PIT 通道 2 不工作 */
    }
    t1 = rdtsc();
    return t1 - t0;
}

void time_init(void) {
    unsigned long long delta;
    if (!cpu_get_info()->has_tsc) return;
    delta = measure_tsc();
    if (delta < CALIBRATE_MS * 1000ULL) return;                 /* 低于 1MHz：不可信，退回数中断 */
    tsc_per_ms = (unsigned int) div64_32(delta, CALIBRATE_MS);
    tsc_start = rdtsc() - delta;   /* uptime 从开始校准那一刻算起（之前的启动过程只有几毫秒） */
}

unsigned int time_ms(void) {
    if (!tsc_per_ms) return pit_ticks() * 10;
    return (unsigned int) div64_32(rdtsc() - tsc_start, tsc_per_ms);
}

const char  *time_source(void)  { return tsc_per_ms ? "tsc" : "pit"; }

unsigned long long time_tsc(void) { return rdtsc(); }

unsigned int time_tsc_to_us(unsigned long long ticks) {
    if (!tsc_per_ms) return 0;
    return (unsigned int) div64_32(ticks * 1000, tsc_per_ms);
}
unsigned int time_tsc_khz(void) { return tsc_per_ms; }
