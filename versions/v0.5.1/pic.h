#ifndef PIC_H
#define PIC_H

/* offset1/offset2：把主/从 PIC 的中断号重新映射到哪里开始。
   BIOS 默认把 IRQ0~7 映到中断号 8~15——正好和 CPU 异常撞车（比如 IRQ0 定时器
   会撞上 8 号"双重故障"异常），所以必须先挪到 32 号以后的空位 */
void pic_remap(int offset1, int offset2);

/* 处理完一个硬件中断后必须调用，告诉 PIC"这条线我处理完了，可以再给我发下一次"，
   不调用的话这条中断线之后会一直"卡住"，再也收不到新的中断 */
void pic_send_eoi(unsigned char irq);

/* 把两片 PIC 的所有中断线全部屏蔽——现在还没写任何具体设备驱动（定时器/键盘），
   先全部关掉，只搭好重映射的地基，不让任何中断真的触发 */
void pic_mask_all(void);

#endif
