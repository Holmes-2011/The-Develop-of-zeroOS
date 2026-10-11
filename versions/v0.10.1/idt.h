#ifndef IDT_H
#define IDT_H

/* 一条 IDT 描述符：告诉 CPU "发生第 N 号中断/异常时，该跳到哪个地址执行" */
struct idt_entry {
    unsigned short base_low;
    unsigned short sel;        /* 目标代码段选择子，对应 GDT 里的 CODE_SEG */
    unsigned char  always0;
    unsigned char  flags;      /* 存在位、特权级、门类型 */
    unsigned short base_high;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

void idt_install(void);
void idt_set_gate(unsigned char num, unsigned int base, unsigned short sel, unsigned char flags);

#endif
