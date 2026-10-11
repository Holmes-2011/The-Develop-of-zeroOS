#include "idt.h"

#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

/* idt_flush 在 isr.asm 里，用 lidt 指令把表地址告诉 CPU */
extern void idt_flush(unsigned int);

/* isr0~isr31：CPU 保留给异常用的前 32 个中断号，入口都在 isr.asm 里 */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

/* irq0~irq15：硬件中断入口，PIC 重映射后对应的中断号是 32~47 */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

void idt_set_gate(unsigned char num, unsigned int base, unsigned short sel, unsigned char flags) {
    idt[num].base_low  = (unsigned short)(base & 0xFFFF);
    idt[num].base_high = (unsigned short)((base >> 16) & 0xFFFF);
    idt[num].sel      = sel;
    idt[num].always0  = 0;
    idt[num].flags    = flags;
}

void idt_install(void) {
    int i;

    idtp.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idtp.base  = (unsigned int) &idt;

    for (i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate((unsigned char) i, 0, 0, 0);   /* 先全部清零，没注册的中断号触发了也不会跑到随机地址去 */
    }

    /* 0x08 = GDT 里的代码段选择子；0x8E = 存在 + ring0 + 32位中断门 */
    idt_set_gate(0,  (unsigned int) isr0,  0x08, 0x8E);
    idt_set_gate(1,  (unsigned int) isr1,  0x08, 0x8E);
    idt_set_gate(2,  (unsigned int) isr2,  0x08, 0x8E);
    idt_set_gate(3,  (unsigned int) isr3,  0x08, 0x8E);
    idt_set_gate(4,  (unsigned int) isr4,  0x08, 0x8E);
    idt_set_gate(5,  (unsigned int) isr5,  0x08, 0x8E);
    idt_set_gate(6,  (unsigned int) isr6,  0x08, 0x8E);
    idt_set_gate(7,  (unsigned int) isr7,  0x08, 0x8E);
    idt_set_gate(8,  (unsigned int) isr8,  0x08, 0x8E);
    idt_set_gate(9,  (unsigned int) isr9,  0x08, 0x8E);
    idt_set_gate(10, (unsigned int) isr10, 0x08, 0x8E);
    idt_set_gate(11, (unsigned int) isr11, 0x08, 0x8E);
    idt_set_gate(12, (unsigned int) isr12, 0x08, 0x8E);
    idt_set_gate(13, (unsigned int) isr13, 0x08, 0x8E);
    idt_set_gate(14, (unsigned int) isr14, 0x08, 0x8E);
    idt_set_gate(15, (unsigned int) isr15, 0x08, 0x8E);
    idt_set_gate(16, (unsigned int) isr16, 0x08, 0x8E);
    idt_set_gate(17, (unsigned int) isr17, 0x08, 0x8E);
    idt_set_gate(18, (unsigned int) isr18, 0x08, 0x8E);
    idt_set_gate(19, (unsigned int) isr19, 0x08, 0x8E);
    idt_set_gate(20, (unsigned int) isr20, 0x08, 0x8E);
    idt_set_gate(21, (unsigned int) isr21, 0x08, 0x8E);
    idt_set_gate(22, (unsigned int) isr22, 0x08, 0x8E);
    idt_set_gate(23, (unsigned int) isr23, 0x08, 0x8E);
    idt_set_gate(24, (unsigned int) isr24, 0x08, 0x8E);
    idt_set_gate(25, (unsigned int) isr25, 0x08, 0x8E);
    idt_set_gate(26, (unsigned int) isr26, 0x08, 0x8E);
    idt_set_gate(27, (unsigned int) isr27, 0x08, 0x8E);
    idt_set_gate(28, (unsigned int) isr28, 0x08, 0x8E);
    idt_set_gate(29, (unsigned int) isr29, 0x08, 0x8E);
    idt_set_gate(30, (unsigned int) isr30, 0x08, 0x8E);
    idt_set_gate(31, (unsigned int) isr31, 0x08, 0x8E);

    /* 32~47 号：重映射后的硬件中断（IRQ0~15） */
    idt_set_gate(32, (unsigned int) irq0,  0x08, 0x8E);
    idt_set_gate(33, (unsigned int) irq1,  0x08, 0x8E);
    idt_set_gate(34, (unsigned int) irq2,  0x08, 0x8E);
    idt_set_gate(35, (unsigned int) irq3,  0x08, 0x8E);
    idt_set_gate(36, (unsigned int) irq4,  0x08, 0x8E);
    idt_set_gate(37, (unsigned int) irq5,  0x08, 0x8E);
    idt_set_gate(38, (unsigned int) irq6,  0x08, 0x8E);
    idt_set_gate(39, (unsigned int) irq7,  0x08, 0x8E);
    idt_set_gate(40, (unsigned int) irq8,  0x08, 0x8E);
    idt_set_gate(41, (unsigned int) irq9,  0x08, 0x8E);
    idt_set_gate(42, (unsigned int) irq10, 0x08, 0x8E);
    idt_set_gate(43, (unsigned int) irq11, 0x08, 0x8E);
    idt_set_gate(44, (unsigned int) irq12, 0x08, 0x8E);
    idt_set_gate(45, (unsigned int) irq13, 0x08, 0x8E);
    idt_set_gate(46, (unsigned int) irq14, 0x08, 0x8E);
    idt_set_gate(47, (unsigned int) irq15, 0x08, 0x8E);

    idt_flush((unsigned int) &idtp);
}
