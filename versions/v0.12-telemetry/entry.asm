; entry.asm —— 内核镜像的第 0 条指令
;
; 引导层（boot.asm）把内核复制到 1MB（0x100000）后跳过来。
; CPU 跳过去时不会读 ELF 头，所以 0x100000 处必须正好是第一条要执行的机器码。
; 链接脚本 link.ld 用 KEEP(*(.text.entry)) 把这一段钉在最前面来保证这一点。
;
; 这个垫片做这几件事：关中断、换上内核自己的 GDT、把 .bss 清零、换上内核自己的栈、
; 把控制权交给 C 写的 kmain。

BITS 32
section .text.entry
global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    ; 保险起见再关一次中断（boot.asm 进保护模式前已经 cli 过）。
    ; 在 C 里建好自己的 IDT 之前，任何硬件中断都会因为找不到合法中断门而三重故障。
    cli

    ; 换上内核自己的 GDT（v0.9）。
    ; 引导扇区里那张 GDT 在 0x7C00 附近的低端内存里，物理内存管理会把 1MB 以下整体保留，
    ; 但让内核的生死依赖"引导扇区那块内存没被动过"总归不踏实：
    ; 每次中断入口重载段寄存器都要读 GDT。内容和引导扇区的一样：平坦的 4GB 代码段 + 数据段
    lgdt [gdt_descriptor]
    jmp 0x08:.reload_cs       ; 远跳转才能重载 CS
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 把 .bss 清零。
    ; C 语言保证没写初值的全局/静态变量一开始都是 0，但 .bss 不占镜像文件的空间，没人替我们清。
    ; 这时候还在用引导扇区的临时栈（0x7C00 以下），清 .bss 不会碰到它
    cld                         ; rep stosb 往高地址方向写
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; 换上内核自己的栈（16KB，在 .bss 里，所以必须在清零之后才能用）
    mov esp, stack_top

    call kmain          ; 进入 C 世界

.hang:                  ; kmain 正常不会返回，返回了也不让它乱跑
    cli
    hlt
    jmp .hang

; 访问字节里的最低位是"已访问"（Accessed）：CPU 第一次把某个段装进段寄存器时会自己把它置 1，
; 也就是说 CPU 会往 GDT 里写。这里预先置好（0x9B / 0x93 而不是 0x9A / 0x92），
; CPU 就不用再写了——这张表放在只读数据区，以后把只读区真正设成只读时不会因此缺页。
; （v0.9 测试时发现：内存里的 GDT 和内核文件差了这一位）
section .rodata
align 8
gdt_start:
    dq 0                                        ; 0x00：空描述符（CPU 规定第一项必须是空的）
    dq 0x00CF9B000000FFFF                       ; 0x08：代码段，基址 0，界限 4GB，ring 0，可读可执行
    dq 0x00CF93000000FFFF                       ; 0x10：数据段，基址 0，界限 4GB，ring 0，可读可写
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
