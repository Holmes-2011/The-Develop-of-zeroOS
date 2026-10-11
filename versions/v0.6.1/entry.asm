; entry.asm —— 内核镜像的第 0 条指令
;
; 引导层（boot.asm）最后执行的是 `jmp KERNEL_LOAD_ADDR`，也就是跳到 0x1000。
; CPU 跳过去时不会读 ELF 头，所以 0x1000 处必须正好是第一条要执行的机器码。
; 链接脚本 link.ld 用 KEEP(*(.text.entry)) 把这一段钉在最前面来保证这一点。
;
; 这个垫片做三件事：关中断、把 .bss 清零、把控制权交给 C 写的 kmain。
; 用垫片而不是直接把 kmain 放在 0x1000，是因为 C 编译器不保证函数的排列顺序 ——
; kmain 在 kernel.c 里排在最后，直接链接的话 0x1000 处会是 print_char。

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

    ; 把 .bss 清零。
    ;
    ; C 语言保证没写初值的全局/静态变量（比如 pit.c 里的 ticks、idt.c 里的 IDT 表）
    ; 一开始都是 0，但 .bss 不占镜像文件的空间，没人替我们清：
    ; 以前它恰好落在 Makefile 补零的那 20KB 里所以是 0，内核一变大就不一定了。
    cld                         ; rep stosb 往高地址方向写
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kmain          ; 进入 C 世界

.hang:                  ; kmain 正常不会返回，返回了也不让它乱跑
    cli
    hlt
    jmp .hang
