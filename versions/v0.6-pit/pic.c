#include "pic.h"
#include "io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_remap(int offset1, int offset2) {
    unsigned char mask1, mask2;

    mask1 = inb(PIC1_DATA);   /* 先把两片 PIC 当前的屏蔽字存起来，重映射完了原样恢复 */
    mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    outb(PIC1_DATA, (unsigned char) offset1);   /* 主片中断号从这里开始 */
    outb(PIC2_DATA, (unsigned char) offset2);   /* 从片中断号从这里开始 */

    outb(PIC1_DATA, 4);   /* 告诉主片：从片挂在它的 IRQ2 线上 */
    outb(PIC2_DATA, 2);   /* 告诉从片：自己的级联编号是 2 */

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    outb(PIC1_DATA, mask1);   /* 恢复原来的屏蔽字，重映射这个动作本身不应该改变"哪些中断是开的" */
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(unsigned char irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);   /* 来自从片的中断，从片也要收到一份 EOI */
    }
    outb(PIC1_COMMAND, 0x20);
}

void pic_mask_all(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask_irq(unsigned char irq) {
    unsigned char mask;
    if (irq < 8) { mask = inb(PIC1_DATA); outb(PIC1_DATA, (unsigned char)(mask & (unsigned char)~(1U << irq))); }
    else if (irq < 16) { mask = inb(PIC2_DATA); outb(PIC2_DATA, (unsigned char)(mask & (unsigned char)~(1U << (irq - 8)))); mask = inb(PIC1_DATA); outb(PIC1_DATA, (unsigned char)(mask & (unsigned char)~(1U << 2))); }
}
