/* link.ld —— 高半内核（v0.15）
 *
 * 虚拟地址：内核在 0xC0100000 起（3GB + 1MB）。0 ~ 3GB 留给用户态程序。
 * 物理地址：引导扇区仍把内核复制到 1MB（0x100000）——AT(...) 让每个段的加载地址 = 虚拟地址 - 0xC0000000。
 * entry.asm 一开始在物理地址上运行（分页还没开），建好临时页表、开分页之后才跳到高地址。
 *
 * elf_to_bin.py 按"加载地址"（p_paddr）排布纯二进制，按物理末端（含 .bss）检查不超过 4MB。
 */

ENTRY(_start)
KERNEL_VBASE = 0xC0000000;

SECTIONS
{
    . = KERNEL_VBASE + 0x100000;

    .text : AT(ADDR(.text) - KERNEL_VBASE) {
        KEEP(*(.text.entry))    /* 必须最靠前，保证物理 0x100000 就是 _start */
        *(.text*)
    }

    .rodata : AT(ADDR(.rodata) - KERNEL_VBASE) { *(.rodata*) }
    .data   : AT(ADDR(.data) - KERNEL_VBASE)   { *(.data*) }
    .bss    : AT(ADDR(.bss) - KERNEL_VBASE) {
        __bss_start = .;        /* entry.asm 用这两个符号把 .bss 清零（减去 KERNEL_VBASE 得到物理地址） */
        *(.bss*) *(COMMON)
        __bss_end = .;
    }

    /* 内核（含 .bss）在虚拟地址空间里的末端。物理页位图紧跟在它后面 */
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
