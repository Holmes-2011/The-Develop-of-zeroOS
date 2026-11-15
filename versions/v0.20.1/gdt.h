#ifndef GDT_H
#define GDT_H

/* 全局描述符表（v0.16）：从 v0.1 起 GDT 一直放在 entry.asm 里写死，只有两个段。
   用户态需要再加三个：用户代码段、用户数据段（DPL=3）、TSS（ring 3 → ring 0 切换时
   CPU 从 TSS 里取内核栈 esp0）。段选择子编号保持和以前一致，0x08 / 0x10 不变，
   这样 IDT 和 isr.asm 里的 0x08 / 0x10 都不用动。 */

#define SEG_KCODE 0x08   /* ring 0 代码段 */
#define SEG_KDATA 0x10   /* ring 0 数据段 */
#define SEG_UCODE 0x1B   /* ring 3 代码段（0x18 | RPL 3） */
#define SEG_UDATA 0x23   /* ring 3 数据段（0x20 | RPL 3） */
#define SEG_TSS   0x28   /* 任务状态段 */

void gdt_init(void);                              /* 建表 + lgdt + ltr */
void tss_set_esp0(unsigned int esp0);             /* 进用户态前把内核栈顶写进 TSS */

#endif
