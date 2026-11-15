; entry.asm —— 内核镜像的第 0 条指令
;
; 引导层（boot.asm）把内核复制到物理 1MB（0x100000）后跳过来。这时分页还没开，CPU 用的是物理地址；
; 而内核是按虚拟地址 0xC0100000 链接的（高半内核，v0.15）。所以这段代码在"跳到高地址"之前
; 引用任何符号都要减去 KERNEL_VBASE，得到物理地址。
;
; 顺序：关中断 → 清 .bss → 建临时页表（物理 0~4MB 同时映到虚拟 0~4MB 和 0xC0000000~）
;       → 开分页 → 跳到高地址 → 换内核自己的 GDT → 填栈图案、换栈 → kmain
; paging_init() 之后会建完整的页表并换掉这张临时表，那时恒等映射（0~4MB）就消失了。

BITS 32
KERNEL_VBASE equ 0xC0000000

section .text.entry
global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    cli

    ; 1. 清 .bss（物理地址：分页还没开）。临时页表就在 .bss 里，必须先清再填
    cld
    mov edi, __bss_start - KERNEL_VBASE
    mov ecx, __bss_end - KERNEL_VBASE
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; 2. 临时页表：1024 项 × 4KB = 物理 0 ~ 4MB，present + writable
    mov edi, boot_pt - KERNEL_VBASE
    mov eax, 0x003
    mov ecx, 1024
.fill:
    mov [edi], eax
    add eax, 0x1000
    add edi, 4
    loop .fill
    ; 页目录：第 0 项（虚拟 0 ~ 4MB，恒等）和第 768 项（虚拟 0xC0000000 ~ +4MB）都指向这张页表
    mov eax, boot_pt - KERNEL_VBASE
    or eax, 3
    mov [boot_pd - KERNEL_VBASE], eax
    mov [boot_pd - KERNEL_VBASE + 768 * 4], eax

    ; 3. 开分页
    mov eax, boot_pd - KERNEL_VBASE
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    mov eax, .higher          ; 绝对跳转到高地址（相对跳转会停在物理地址那一侧）
    jmp eax

.higher:
    ; 从这里起全是虚拟地址
    lgdt [gdt_descriptor]
    jmp 0x08:.reload_cs
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 把内核栈填成固定图案（v0.13.2）：stack.c 扫描"图案从哪里开始被覆盖"就知道栈最多用过多少；
    ; 栈底第一个字是哨兵，被改了就是溢出
    mov edi, stack_bottom
    mov ecx, 16384 / 4
    mov eax, 0x5AC0FFEE
    rep stosd
    mov esp, stack_top

    call kmain

.hang:                  ; kmain 正常不会返回，返回了也不让它乱跑
    cli
    hlt
    jmp .hang

; 访问字节里的最低位是"已访问"（Accessed）：CPU 第一次把某个段装进段寄存器时会自己把它置 1。
; 这里预先置好（0x9B / 0x93），CPU 就不用再往这张只读表里写了
section .rodata
align 8
gdt_start:
    dq 0                                        ; 0x00：空描述符
    dq 0x00CF9B000000FFFF                       ; 0x08：代码段，基址 0，界限 4GB，ring 0
    dq 0x00CF93000000FFFF                       ; 0x10：数据段，基址 0，界限 4GB，ring 0
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .bss
align 4096
boot_pd:                    ; 临时页目录 / 页表（只在开机到 paging_init 之间用）
    resb 4096
boot_pt:
    resb 4096
align 16
global stack_bottom
global stack_top
stack_bottom:
    resb 16384
stack_top:
