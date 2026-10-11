#ifndef KEYBOARD_H
#define KEYBOARD_H

/* PS/2 键盘驱动（IRQ1）。
   中断里只做"读扫描码 → 翻译成字符 → 放进环形缓冲区"，
   真正处理字符（回显、拼成一行）交给主循环——中断处理越短越好 */

void keyboard_init(void);    /* 清空键盘控制器里残留的字节；之后由调用方放开 IRQ1 */
void keyboard_irq(void);     /* IRQ1 触发时由 isr_handler 调用 */
int  keyboard_getchar(void); /* 取一个字符；缓冲区空时返回 -1。只能在主循环里调用 */

#endif
