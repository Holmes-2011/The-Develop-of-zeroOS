; entry.asm —— 内核镜像的第 0 条指令
;
; 引导层（zeroOs 引导层.asm）最后执行的是 `jmp KERNEL_LOAD_ADDR`，也就是跳到 0x1000。
; CPU 跳过去时不会读 ELF 头，所以 0x1000 处必须正好是第一条要执行的机器码。
; 链接脚本 link.ld 用 KEEP(*(.text.entry)) 把这一段钉在最前面来保证这一点。
;
; 这个垫片只做一件事：把控制权交给 C 写的 kmain。
; 用垫片而不是直接把 kmain 放在 0x1000，是因为 C 编译器不保证函数的排列顺序 ——
; kmain 在 kernel.c 里排在最后，直接链接的话 0x1000 处会是 print_char。

BITS 32
section .text.entry
global _start
extern kmain

_start:
    ; ⚠️ 必须在这里关中断。
    ;
    ; 引导层为了用 BIOS 读盘，很早就执行了 `sti` 打开中断；进入保护模式前
    ; 只 `lgdt` 了段表，**没有 `lidt`** —— 也就是说 IDT 寄存器里还是实模式那套
    ; 中断向量表（基址 0，长度 0x3FF）。
    ;
    ; 保护模式下 CPU 会用「保护模式描述符」的格式去解析这张表，结果是垃圾。
    ; 于是上电后第一个时钟中断（IRQ0，约每 55ms 一次）一到，CPU 取不到合法
    ; 的中断门 → 三重故障 → 整机重启，屏幕只打出几个字符就黑掉，
    ; 而且会无限循环（重启后又跑一遍）。
    ;
    ; 所以在建立自己的 IDT 之前，先把中断关掉。
    ; 以后要支持键盘/时钟，就在 C 里建好 IDT 之后再 `sti`。
    cli

    call kmain          ; 进入 C 世界

.hang:                  ; kmain 正常不会返回，返回了也不让它乱跑
    cli
    hlt
    jmp .hang
