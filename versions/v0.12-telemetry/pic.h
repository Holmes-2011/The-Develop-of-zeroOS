#ifndef PIC_H
#define PIC_H

/* offset1/offset2：把主/从 PIC 的中断号重新映射到哪里开始。
   BIOS 默认把 IRQ0~7 映到中断号 8~15——正好和 CPU 异常撞车（比如 IRQ0 定时器
   会撞上 8 号"双重故障"异常），所以必须先挪到 32 号以后的空位 */
void pic_remap(int offset1, int offset2);

/* 处理完一个硬件中断后必须调用，告诉 PIC"这条线我处理完了，可以再给我发下一次"，
   不调用的话这条中断线之后会一直"卡住"，再也收不到新的中断 */
void pic_send_eoi(unsigned char irq);

/* 把两片 PIC 的所有中断线全部屏蔽——之后再用 pic_unmask_irq 只放开有驱动的那几条线 */
void pic_mask_all(void);

/* 放开某一条中断线；放开从片上的线（8~15）时，会顺带放开主片上级联用的 IRQ2 */
void pic_unmask_irq(unsigned char irq);

/* 判断 IRQ7 / IRQ15 是不是"伪中断"：中断线在 CPU 应答前就撤销了，PIC 只好报一个最低优先级的号。
   真机上偶尔会出现。伪中断不能照常发 EOI，否则会把别的正在服务的中断"确认"掉。
   返回 1 表示是伪中断（必要的 EOI 已经在函数里处理好），调用方直接返回即可 */
int pic_is_spurious(unsigned char irq);

#endif
