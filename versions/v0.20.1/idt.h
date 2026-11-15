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

/* isr_common_stub 保存下来的现场（v0.16 起放在这里共享，kernel.c 和 usermode.c 都用）。
   字段顺序必须和 isr.asm 的压栈顺序严格一致：
     ds（stub 自己压的）→ pusha 的 8 个 → 中断号 → 错误码 → CPU 压的 eip/cs/eflags
     从 ring 3 进来时 CPU 还会多压 user_esp / user_ss（只有中断发生时特权级变了才有） */
struct registers {
    unsigned int ds;
    unsigned int edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags;
    unsigned int user_esp, user_ss;
};

void idt_install(void);
void idt_set_gate(unsigned char num, unsigned int base, unsigned short sel, unsigned char flags);

/* 异常号 → 可读名字（kernel.c 里那张表的访问入口） */
const char *exception_message(unsigned int no);

#endif
