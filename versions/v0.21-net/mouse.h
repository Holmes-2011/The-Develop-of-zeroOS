#ifndef MOUSE_H
#define MOUSE_H

/* PS/2 鼠标（v0.19）：走 8042 的 aux 端口，IRQ12。
   启动时打开 aux 通道和 IRQ12，然后让鼠标以 3 字节包持续上报
   （flags / dx / dy）。坐标是"自上次读取以来的增量"，由 wm.c 累加。 */

void mouse_init(void);                    /* 在开中断 / 键盘初始化之后调用 */
void mouse_irq(void);                     /* IRQ12 时由 isr_handler 调用 */

/* 取一次累计状态：返回自上次调用以来有没有新数据；dx/dy 是增量，buttons 是当前按键 */
int  mouse_poll(int *dx, int *dy, int *buttons);
int  mouse_ready(void);

#endif
