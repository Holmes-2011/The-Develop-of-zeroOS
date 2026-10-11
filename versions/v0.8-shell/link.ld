/* link.ld —— 把 C 内核链接到引导层约定的加载地址
 *
 * 关键约定（必须和 boot.asm 里的 KERNEL_LOAD_ADDR 一致）：
 *   0x1000  —— 内核被 BIOS 读入的内存地址，也是 CPU 跳转过来的入口
 *
 * 用 elf_to_bin.py（等价于 objcopy -O binary）把 ELF 转成纯二进制时，只有被装入的段会被写出，
 * 所以这里的地址布局直接决定了内核在内存里的样子。
 */

ENTRY(_start)

SECTIONS
{
    . = 0x1000;

    .text : {
        KEEP(*(.text.entry))    /* 必须最靠前，保证 0x1000 就是 _start */
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : {
        __bss_start = .;        /* entry.asm 用这两个符号把 .bss 清零 */
        *(.bss*) *(COMMON)
        __bss_end = .;
    }

    /* 内核（含 .bss）在内存里的末端。必须 < 0x7C00：那里是引导扇区和 GDT，
       每次中断入口重载段寄存器都要读 GDT。Makefile 用 elf_to_bin.py --mem-limit 检查 */
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
