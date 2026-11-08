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
 * v0.9：内核搬到 1MB；物理页分配（pmm.c）、分页（paging.c）、内核堆（heap.c）。
 *       缺页异常（#PF）会报出是哪个地址、读还是写。
 * v0.10：开启 FPU / SSE2（cpu.c），数学函数（kmath.c），mathtest / bench 命令。
 *        本文件属于"核心代码"，用 -mno-sse -mno-80387 编译，不能有任何浮点运算。
 * v0.10.1：开机时间改用 TSC（time.c），满负荷时不再因为丢时钟中断而变慢；
 *          堆会把末尾空出来的页还给物理页分配器。
 * v0.11：块设备接口（blockdev.c）+ ATA PIO 硬盘驱动（ata.c）。
 * v0.12：感知层（telemetry.c）：各子系统的计数器、每 100 毫秒采样一次、事件日志；
 *        按需分页区（demand.c）：那段地址里的缺页现场补一页、继续运行，而不是停机。
 * v0.13：工具层（tools.c）：工具登记、权限等级、内核绘制的确认框（confirm.c）、审计日志；
 *        人用的 `tool` 命令和 Agent（v0.14 起走串口协议，以后走系统调用）走同一个入口 tools_invoke()。
 * v0.13.1：修正 inject_fault spin 不带数字时的越界读；memtest 的按需分页检查改为相对值。
 * v0.15：高半内核：内核链接到 0xC0100000，物理内存映射到 0xC0000000 起，0 ~ 3GB 留给用户态。
 *        行为对用户不变；所有碰物理页的地方改经 P2V()。
 * v0.14：桥接协议内核侧（bridge.c、kjson.c、serial.c 的接收）：宿主机上的 Agent 通过 COM1
 *        调用工具、提交直觉判断；内核推送事件。Agent 第一次能操作 zeroOS。
 * v0.13.2：全量复审 + 压力测试后的修正：引导扇区验证 A20；键盘跟踪 Ctrl / Alt；
 *          遥测流保证从串口行首开始；栈水位 / 溢出检测（stack.c）；分页初始化失败即报错；
 *          堆一致性检查防成环；sleep 计入空闲时间；memcpy / memset 改用 rep 指令。
 */

#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "serial.h"
#include "console.h"
#include "shell.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "cpu.h"
#include "time.h"
#include "ata.h"
#include "tools.h"
#include "telemetry.h"
#include "demand.h"
#include "bridge.h"

const char *kernel_version_string(void) { return "v0.15"; }

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
        if (regs->int_no == 14) {
            /* 按需分页区里的缺页：补一页、返回，CPU 会重新执行那条指令（见 demand.h） */
            unsigned int cr2;
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
            if (demand_handle_fault(cr2, regs->err_code)) return;
        }
        /* 屏幕和串口都打：屏幕给人看，串口给测试和调试看 */
        console_set_color(VGA_LIGHT_RED);
        console_write("\nzeroOS kernel: CPU exception -> ");
        console_write(exception_messages[regs->int_no]);
        console_write(" at EIP ");
        console_write_hex(regs->eip);
        if (regs->int_no == 14) {
            /* 缺页：CPU 把"访问的是哪个地址"放在 CR2 里，错误码说明是怎么访问的 */
            unsigned int cr2;
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
            console_write("\n  address ");
            console_write_hex(cr2);
            console_write(regs->err_code & 2 ? " (write, " : " (read, ");
            console_write(regs->err_code & 1 ? "protection violation)" : "page not mapped)");
            if (cr2 < 0x1000) console_write(" -- null pointer?");
        }
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
        if (pic_is_spurious(irq)) {         /* 伪中断不能发 EOI，见 pic.h */
            tel_spurious(irq);
            return;
        }
        tel_irq(irq);
        if (irq == 0) {
            pit_tick();
            tel_timer_tick();               /* 到 100 毫秒了就给所有计数器拍一次快照 */
        }
        if (irq == 1) keyboard_irq();
        if (irq == 4) serial_irq();          /* v0.14：COM1 收到字节 */
        pic_send_eoi(irq);
    }
}

void kmain(void) {
    /* 启动日志只走串口：屏幕留给用户，不打一堆调试信息 */
    serial_init();
    serial_write_string("zeroOS kernel v0.15: entered protected mode, kmain() started\n");

    idt_install();
    serial_write_string("zeroOS kernel v0.15: IDT installed, CPU exceptions now caught\n");

    pic_remap(0x20, 0x28);   /* 把 IRQ0~15 挪到 32~47 号，避开和 CPU 异常撞车 */
    pic_mask_all();
    pit_init(100);
    pic_unmask_irq(0);
    serial_write_string("zeroOS kernel v0.15: PIT at 100 Hz, IRQ0 enabled\n");
    keyboard_init();
    pic_unmask_irq(1);
    serial_write_string("zeroOS kernel v0.15: PS/2 keyboard on IRQ1 enabled\n");

    /* 内存：先有物理页分配，分页要从它那里拿页表；堆又要靠分页把页映射过去 */
    cpu_init();
    if (cpu_get_info()->float_enabled) {
        serial_write_string("zeroOS kernel v0.15: FPU + SSE2 enabled\n");
    } else {
        serial_write_string("zeroOS kernel v0.15: no SSE2 on this CPU, floating point disabled\n");
    }
    time_init();   /* 必须在开中断之前：校准要独占 50 毫秒 */
    serial_write_string(time_tsc_khz() ? "zeroOS kernel v0.15: clock source: TSC\n"
                                       : "zeroOS kernel v0.15: clock source: timer interrupts (no TSC)\n");

    pmm_init();
    serial_write_string("zeroOS kernel v0.15: physical memory manager ready\n");
    paging_init();
    serial_write_string("zeroOS kernel v0.15: paging: higher-half kernel at 0xC0100000, low 3GB unmapped\n");
    heap_init();
    serial_write_string("zeroOS kernel v0.15: kernel heap ready at 0xF0000000\n");
    tel_init();   /* 开中断之前：第一次采样定在 100 毫秒之后 */
    serial_write_string("zeroOS kernel v0.15: telemetry sampling every 100 ms\n");

    __asm__ __volatile__("sti");

    {   /* 硬盘：放在开中断之后，等待超时要靠时钟 */
        int n = ata_init();
        serial_write_string(n ? "zeroOS kernel v0.15: ATA disks found: " : "zeroOS kernel v0.15: no ATA disks\n");
        if (n) {
            serial_write_char((char) ('0' + n));
            serial_write_char('\n');
        }
    }

    vga_clear();
    serial_write_string("zeroOS kernel v0.15: shell ready on screen and COM1\n");
    tel_event(EV_BOOT, 0);
    tools_init();
    bridge_init();                           /* v0.14：放开串口接收中断，等宿主机的 hello */
    pic_unmask_irq(4);
    serial_write_string("zeroOS kernel v0.15: agent bridge listening on COM1 (protocol v1)\n");
    serial_write_string("zeroOS kernel v0.15: tool layer ready, developer mode off\n");

    /* 想验证异常处理确实生效，可以临时取消下面这行注释，触发一次除零异常：
       屏幕和串口都会打印 "Division By Zero" 然后安全停机，而不是让 QEMU 窗口静默重启
       __asm__ __volatile__("int $0x0");
    */

    console_set_color(VGA_WHITE);
    console_write("zeroOS v0.15\n");
    console_set_color(VGA_LIGHT_GREY);
    console_write("Type 'help' for a list of commands.\n\n");

    shell_run();
}
