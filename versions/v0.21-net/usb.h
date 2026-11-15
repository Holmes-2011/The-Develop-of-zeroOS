#ifndef USB_H
#define USB_H

/* USB 平板（v0.20）：PCI 找 UHCI → 端口复位 → 枚举 usb-tablet（HID 中断 IN）
   → 读绝对坐标。不依赖 PS/2，UTM / QEMU 的 usb-tablet 都能用。 */

int  usb_tablet_init(void);                        /* 返回 1 = 找到并配置好 */
int  usb_tablet_poll(int *x, int *y, int *btn);    /* 有新报返回 1；x/y 是屏幕坐标 */

#endif
