/*
 * kernel.c —— 真正用 C 写的部分。
 *
 * v0.1：清屏、停机，屏幕保持干净无画面。
 * v0.2：加了串口调试输出——走 COM1（I/O 端口 0x3F8），不经过屏幕。
 * v0.3：加了 IDT 异常处理（idt.c/idt.h/isr.asm）——以前 CPU 遇到异常（除零、缺页……）
 *       会直接三重故障、整个虚拟机静默重启，现在能捕获到，并打印出是哪种异常再安全停机。
 * v0.5：加了 PIC 重映射（pic.c/pic.h）+ IRQ0~15 入口，先把地基搭好。
 * v0.6：加了 PIT 定时器（pit.c/pit.h）：100 Hz，放开 IRQ0 并开中断，主循环每秒打一行 tick。
 * v0.6.1：entry.asm 清零 .bss；中断入口 cld；处理 IRQ7/15 伪中断；构建时检查内存上限。
 * v0.7：加了 PS/2 键盘驱动（keyboard.c/keyboard.h，IRQ1）。按键回显到串口，
 *       回车后把整行打印出来——这是 v0.8 命令行的雏形。去掉了每秒一行的 timer tick。
 */

#include "idt.h"
#include "pic.h"
#include "io.h"
#include "pit.h"
#include "keyboard.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* 先关掉串口中断，咱们用轮询方式，不需要中断 */
    outb(COM1 + 3, 0x80);   /* 打开 DLAB，接下来两次写入是在设置波特率 */
    outb(COM1 + 0, 0x03);   /* 波特率除数低字节 —— 对应 38400 波特率 */
    outb(COM1 + 1, 0x00);   /* 波特率除数高字节 */
    outb(COM1 + 3, 0x03);   /* 8 位数据位、无校验、1 位停止位，关闭 DLAB */
    outb(COM1 + 2, 0xC7);   /* 打开 FIFO，并清空收发缓冲区 */
    outb(COM1 + 4, 0x0B);   /* 打开 IRQ 相关线路（虽然用的是轮询，这几位还是按标准置上） */
}

static int serial_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;   /* 第5位=1代表发送缓冲区空了，可以继续写下一个字节 */
}

void serial_write_char(char c) {
    while (!serial_transmit_empty()) { }   /* 缓冲区没空之前先等着，别把字节冲掉 */
    outb(COM1, c);
}

void serial_write_string(const char *str) {
    while (*str) {
        serial_write_char(*str);
        str++;
    }
}

/* 打印一个无符号十进制数（还没有 printf） */
void serial_write_dec(unsigned int n) {
    char digits[10];
    int i = 0;
    do {
        digits[i++] = (char) ('0' + n % 10);
        n /= 10;
    } while (n);
    while (i) serial_write_char(digits[--i]);
}

void clear_screen(void) {
    volatile char *video = (volatile char *) 0xb8000;
    int i;
    for (i = 0; i < 80 * 25 * 2; i += 2) {
        video[i]     = ' ';
        video[i + 1] = 0x00;   /* 黑底黑字，屏幕看起来是干净的纯黑 */
    }
}

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
        serial_write_string("zeroOS kernel: CPU exception -> ");
        serial_write_string(exception_messages[regs->int_no]);
        serial_write_string("\n");

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

/* 主循环：从键盘缓冲区取字符，回显到串口，拼成一行；回车时把整行打印出来 */
#define LINE_MAX 79
static void input_loop(void) {
    char line[LINE_MAX + 1];
    int len = 0;

    while (1) {
        int c;

        /* 关中断后再检查缓冲区：如果检查完、hlt 之前恰好来了中断，
           CPU 就会带着"已经有字符"的状态睡下去，要等下一次中断才醒。
           "sti; hlt" 连在一起没有这个问题：sti 之后的那一条指令执行完才会响应中断 */
        __asm__ __volatile__("cli");
        c = keyboard_getchar();
        if (c < 0) {
            __asm__ __volatile__("sti; hlt");
            continue;
        }
        __asm__ __volatile__("sti");

        if (c == '\n') {
            line[len] = '\0';
            serial_write_string("\nzeroOS input: ");
            serial_write_string(line);
            serial_write_string(" (ticks=");
            serial_write_dec(pit_ticks());
            serial_write_string(")\n");
            len = 0;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                serial_write_string("\b \b");   /* 终端上退一格、用空格盖掉、再退一格 */
            }
        } else if (c >= ' ' && c <= '~' && len < LINE_MAX) {
            line[len++] = (char) c;
            serial_write_char((char) c);
        }
    }
}

void kmain(void) {
    serial_init();
    serial_write_string("zeroOS kernel v0.7: entered protected mode, kmain() started\n");

    idt_install();
    serial_write_string("zeroOS kernel v0.7: IDT installed, CPU exceptions now caught\n");

    pic_remap(0x20, 0x28);   /* 把 IRQ0~15 挪到 32~47 号，避开和 CPU 异常撞车 */
    pic_mask_all();
    pit_init(100);
    pic_unmask_irq(0);
    serial_write_string("zeroOS kernel v0.7: PIT at 100 Hz, IRQ0 enabled\n");
    keyboard_init();
    pic_unmask_irq(1);
    serial_write_string("zeroOS kernel v0.7: PS/2 keyboard on IRQ1 enabled\n");
    __asm__ __volatile__("sti");

    clear_screen();
    serial_write_string("zeroOS kernel v0.7: screen cleared, waiting for keyboard input\n");

    /* 想验证异常处理确实生效，可以临时取消下面这行注释，触发一次除零异常：
       串口会打印 "Division By Zero" 然后安全停机，而不是让 QEMU 窗口静默重启
       __asm__ __volatile__("int $0x0");
    */

    input_loop();
}
