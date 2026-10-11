#include "cpu.h"
#include "kstring.h"

static struct cpu_info info;

static void cpuid(unsigned int leaf, unsigned int *a, unsigned int *b, unsigned int *c, unsigned int *d) {
    __asm__ __volatile__("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

/* 有些老 CPU（早期的 486）没有 CPUID 指令，执行它就是"非法指令"。
   判断方法：EFLAGS 的第 21 位（ID 位）能被软件翻转，就说明支持 CPUID */
static int has_cpuid(void) {
    unsigned int before, after;
    __asm__ __volatile__(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl"                    /* 把 EFLAGS 恢复原样 */
        : "=&r"(before), "=&r"(after));
    return ((before ^ after) & 0x200000) != 0;
}

void cpu_init(void) {
    unsigned int a, b, c, d, max_ext, i;
    unsigned int *brand = (unsigned int *) info.brand;

    if (!has_cpuid()) {
        memcpy(info.vendor, "(no CPUID)", 11);
        return;                    /* 不支持 CPUID 的 CPU 也不可能支持 SSE */
    }

    /* 厂商字符串：EBX、EDX、ECX 三个寄存器拼起来，比如 "GenuineIntel" */
    cpuid(0, &a, &b, &c, &d);
    memcpy(info.vendor + 0, &b, 4);
    memcpy(info.vendor + 4, &d, 4);
    memcpy(info.vendor + 8, &c, 4);
    info.vendor[12] = '\0';

    cpuid(1, &a, &b, &c, &d);
    info.has_fpu  = (d >> 0) & 1;
    info.has_fxsr = (d >> 24) & 1;   /* FXSAVE / FXRSTOR：开启 SSE 的前提 */
    info.has_sse  = (d >> 25) & 1;
    info.has_sse2 = (d >> 26) & 1;
    info.has_sse3 = (c >> 0) & 1;

    /* 型号字符串在扩展叶 0x80000002 ~ 0x80000004 里，每叶 16 字节 */
    cpuid(0x80000000, &max_ext, &b, &c, &d);
    if (max_ext >= 0x80000004) {
        for (i = 0; i < 3; i++) {
            cpuid(0x80000002 + i, &brand[i * 4], &brand[i * 4 + 1], &brand[i * 4 + 2], &brand[i * 4 + 3]);
        }
    }
    info.brand[48] = '\0';

    if (!(info.has_fpu && info.has_fxsr && info.has_sse && info.has_sse2)) return;

    /* CR0：清 EM（第 2 位，置 1 表示"没有浮点单元、浮点指令一律报错"），
            置 MP（第 1 位）和 NE（第 5 位，浮点错误走 #MF 异常而不是老式的 IRQ13）
       CR4：置 OSFXSR（第 9 位，操作系统支持 FXSAVE，开启 SSE 指令）
            和 OSXMMEXCPT（第 10 位，SSE 浮点错误走 #XM 异常）
       然后 fninit 把 x87 复位到默认状态。MXCSR 开机默认 0x1F80：所有 SSE 浮点异常都被屏蔽，
       除零得到 inf、0/0 得到 NaN，而不是报异常——这正是数值计算想要的 */
    __asm__ __volatile__(
        "mov %%cr0, %%eax\n\t"
        "and $~0x4, %%eax\n\t"
        "or  $0x22, %%eax\n\t"
        "mov %%eax, %%cr0\n\t"
        "mov %%cr4, %%eax\n\t"
        "or  $0x600, %%eax\n\t"
        "mov %%eax, %%cr4\n\t"
        "fninit"
        : : : "eax", "memory");
    info.float_enabled = 1;
}

const struct cpu_info *cpu_get_info(void) { return &info; }
