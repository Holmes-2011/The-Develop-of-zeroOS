#ifndef CPU_H
#define CPU_H

/* CPU 信息（CPUID）和浮点 / SSE 的开启。

   为什么开了 SSE 却不用在中断时保存 XMM 寄存器：
   zeroOS 现在只有一条执行流（没有多任务），中断处理程序和其他"核心"代码
   都用 -mno-sse -mno-80387 编译，根本不碰浮点 / XMM 寄存器（Makefile 里有检查）。
   中断打断一段 SSE 计算，回来时 XMM 寄存器原封不动。
   以后有了多任务，切换任务时就必须用 FXSAVE / FXRSTOR 保存和恢复它们 */

struct cpu_info {
    char vendor[13];         /* 比如 "GenuineIntel"；CPU 不支持 CPUID 时是 "(no CPUID)" */
    char brand[49];          /* 处理器型号字符串；CPU 不支持时为空 */
    int  has_fpu, has_fxsr, has_sse, has_sse2, has_sse3;
    int  has_tsc;            /* 时间戳计数器（RDTSC），time.c 用它计时 */
    int  invariant_tsc;      /* TSC 频率恒定，不随省电降频变化（CPUID 0x80000007） */
    int  float_enabled;      /* cpu_init 成功开启了 FPU + SSE2 */
};

void                   cpu_init(void);   /* 读 CPUID；支持的话开启 FPU 和 SSE */
const struct cpu_info *cpu_get_info(void);

#endif
