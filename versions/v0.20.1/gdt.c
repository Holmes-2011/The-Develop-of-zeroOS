#include "gdt.h"
#include "kstring.h"

/* 一条 GDT 描述符（8 字节，x86 保护模式的老格式） */
struct gdt_entry {
    unsigned short limit_lo;
    unsigned short base_lo;
    unsigned char  base_mid;
    unsigned char  access;
    unsigned char  gran;      /* 高 4 位是标志，低 4 位是界限的 16~19 位 */
    unsigned char  base_hi;
} __attribute__((packed));

struct gdt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

/* 32 位 TSS（104 字节）。ring 3 中断 / 系统调用进来时，CPU 从 ss0:esp0 取内核栈。
   其余字段用不到，但必须存在且格式正确 */
struct tss_entry {
    unsigned int prev;
    unsigned int esp0, ss0;
    unsigned int esp1, ss1;
    unsigned int esp2, ss2;
    unsigned int cr3, eip, eflags;
    unsigned int eax, ecx, edx, ebx, esp, ebp, esi, edi;
    unsigned int es, cs, ss, ds, fs, gs, ldt;
    unsigned short trap;
    unsigned short iomap;     /* I/O 位图的偏移；设到 TSS 末尾之外 = ring 3 禁止所有端口访问 */
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr   gp;
static struct tss_entry tss;

/* usermode_asm.asm 里：lgdt + 重装段寄存器 */
extern void gdt_flush(struct gdt_ptr *p);

static void set_entry(int i, unsigned int base, unsigned int limit,
                      unsigned char access, unsigned char gran) {
    gdt[i].limit_lo = (unsigned short)(limit & 0xFFFF);
    gdt[i].base_lo  = (unsigned short)(base & 0xFFFF);
    gdt[i].base_mid = (unsigned char)((base >> 16) & 0xFF);
    gdt[i].access   = access;
    gdt[i].gran     = (unsigned char)((gran & 0xF0) | ((limit >> 16) & 0x0F));
    gdt[i].base_hi  = (unsigned char)((base >> 24) & 0xFF);
}

void tss_set_esp0(unsigned int esp0) {
    tss.esp0 = esp0;
}

void gdt_init(void) {
    unsigned int base  = (unsigned int) &tss;
    unsigned int limit = sizeof(tss) - 1;

    set_entry(0, 0, 0, 0, 0);                    /* 0x00：空描述符，CPU 要求 */
    set_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);        /* 0x08：ring 0 代码，4GB 平坦 */
    set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);        /* 0x10：ring 0 数据 */
    set_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);        /* 0x18：ring 3 代码（FA = 存在 | DPL3 | 代码 | 可读） */
    set_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);        /* 0x20：ring 3 数据 */
    set_entry(5, base, limit, 0x89, 0x00);       /* 0x28：32 位可用 TSS（89 = 存在 | DPL0 | 类型 9） */

    memset(&tss, 0, sizeof(tss));
    tss.ss0  = SEG_KDATA;                        /* 切回 ring 0 用内核数据段 */
    tss.iomap = sizeof(tss);                     /* 偏移 104 > 界限 103：没有 I/O 位图，ring 3 不能读写端口 */

    gp.limit = (unsigned short)(sizeof(gdt) - 1);
    gp.base  = (unsigned int) gdt;
    gdt_flush(&gp);

    /* 把 TSS 装进任务寄存器；之后再进 ring 3，CPU 就知道内核栈在哪 */
    __asm__ __volatile__("ltr %%ax" : : "a"(SEG_TSS));
}
