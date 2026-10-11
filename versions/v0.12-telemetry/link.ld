/* link.ld —— 把 C 内核链接到引导层约定的地址
 *
 * 关键约定（必须和 boot.asm 里的 KERNEL_PHYS 一致）：
 *   0x100000（1MB）—— 引导层把内核复制到这里，然后跳过来
 *
 * v0.9 以前内核在 0x1000，被夹在中断向量表和引导扇区（0x7C00）之间，最多约 27KB。
 *
 * 用 elf_to_bin.py（等价于 objcopy -O binary）把 ELF 转成纯二进制时，只有被装入的段会被写出，
 * 所以这里的地址布局直接决定了内核在内存里的样子。
 */

ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : {
        KEEP(*(.text.entry))    /* 必须最靠前，保证 0x100000 就是 _start */
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : {
        __bss_start = .;        /* entry.asm 用这两个符号把 .bss 清零 */
        *(.bss*) *(COMMON)
        __bss_end = .;
    }

    /* 内核（含 .bss）在内存里的末端。物理内存管理从这里往后（按 4KB 对齐）放自己的位图，
       并把 1MB ~ 位图末尾整段标成"已占用" */
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
