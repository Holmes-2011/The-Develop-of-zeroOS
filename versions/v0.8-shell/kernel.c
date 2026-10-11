/*
 * kernel.c —— 中断分发 + 内核入口 kmain()。
 *
 * v0.1：清屏、停机，屏幕保持干净无画面。
 * v0.2：加了串口调试输出——走 COM1（I/O 端口 0x3F8），不经过屏幕。
 * v0.3：加了 IDT 异常处理（idt.c/idt.h/isr.asm）——以前 CPU 遇到异常（除零、缺页……）
 *       会直接三重故障、整个虚拟机静默重启，现在能捕获到，并打印出是哪种异常再安全停机。
 * v0.5：加了 PIC 重映射（pic.c/pic.h）+ IRQ0~15 入口，先把地基搭好。
 * v0.6：加了 PIT 定时器（pit.c/pit.h）：100 Hz，放开 IRQ0 并开中断。
 * v0.6.1：entry.asm 清零 .bss；中断入口 cld；处理 IRQ7/15 伪中断；构建时检查内存上限。
 * v0.7：加了 PS/2 键盘驱动（keyboard.c/keyboard.h，IRQ1）。
 * v0.8：屏幕终于有字了（vga.c）；串口函数拆到 serial.c；console.c 同时写屏幕和串口；
 *       行输入从这里搬到 shell.c，变成真正的命令行（help / uptime / mem ……）。
 */

#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "serial.h"
#include "console.h"
#include "shell.h"

/* isr.asm 里每个异常入口最终都会统一跳到这里，regs 指向被保存下来的寄存器现场 */
struct registers {
    unsigned int ds;
    unsigned int edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags;
};

static const char *exception_messages[32] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

/* isr_common_stub 统一跳到这里——用 int_no 区分是"CPU 异常"(0~31)还是"硬件中断"(32~47) */
void isr_handler(struct registers *regs) {
    if (regs->int_no < 32) {
        /* 屏幕和串口都打：屏幕给人看，串口给测试和调试看 */
        console_set_color(VGA_LIGHT_RED);
        console_write("\nzeroOS kernel: CPU exception -> ");
        console_write(exception_messages[regs->int_no]);
        console_write(" at EIP ");
        console_write_hex(regs->eip);
        console_write("\nsystem halted.\n");

        /* 现在还没有"恢复/杀掉出问题的任务"的能力，异常了就安全停机——
           比带着错误状态继续往下跑、搞出更难排查的连锁问题要稳得多 */
        while (1) {
            __asm__ __volatile__("hlt");
        }
    } else {
        /* 硬件中断：交给对应的驱动，然后必须发 EOI——
           不发的话 PIC 会以为这条中断线一直没处理完，之后再也收不到新的中断了 */
        unsigned char irq = (unsigned char) (regs->int_no - 32);
        if (pic_is_spurious(irq)) return;   /* 伪中断不能发 EOI，见 pic.h */
        if (irq == 0) pit_tick();
        if (irq == 1) keyboard_irq();
        pic_send_eoi(irq);
    }
}

void kmain(void) {
    /* 启动日志只走串口：屏幕留给用户，不打一堆调试信息 */
    serial_init();
    serial_write_string("zeroOS kernel v0.8: entered protected mode, kmain() started\n");

    idt_install();
    serial_write_string("zeroOS kernel v0.8: IDT installed, CPU exceptions now caught\n");

    pic_remap(0x20, 0x28);   /* 把 IRQ0~15 挪到 32~47 号，避开和 CPU 异常撞车 */
    pic_mask_all();
    pit_init(100);
    pic_unmask_irq(0);
    serial_write_string("zeroOS kernel v0.8: PIT at 100 Hz, IRQ0 enabled\n");
    keyboard_init();
    pic_unmask_irq(1);
    serial_write_string("zeroOS kernel v0.8: PS/2 keyboard on IRQ1 enabled\n");
    __asm__ __volatile__("sti");

    vga_clear();
    serial_write_string("zeroOS kernel v0.8: shell ready on screen and COM1\n");

    /* 想验证异常处理确实生效，可以临时取消下面这行注释，触发一次除零异常：
       屏幕和串口都会打印 "Division By Zero" 然后安全停机，而不是让 QEMU 窗口静默重启
       __asm__ __volatile__("int $0x0");
    */

    console_set_color(VGA_WHITE);
    console_write("zeroOS v0.8\n");
    console_set_color(VGA_LIGHT_GREY);
    console_write("Type 'help' for a list of commands.\n\n");

    shell_run();
}
