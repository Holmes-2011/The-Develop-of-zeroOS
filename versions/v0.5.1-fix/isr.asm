; isr.asm —— 0~31 号 CPU 异常的入口桩
;
; 为什么需要汇编：CPU 触发异常时会自己往栈上压一些东西（有的异常还会多压一个错误码），
; 这个时机 C 函数接管不了——必须先用汇编把现场"统一格式化"好，再转交给 C 写的处理函数。

BITS 32

; 没有错误码的异常：手动压一个占位的 0，让栈结构和"有错误码"的异常保持一致
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    cli
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

; CPU 会自动压错误码的异常（8, 10~14 等），不用再手动压
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    cli
    push dword %1
    jmp isr_common_stub
%endmacro

; ─────────────────────────────────────────────────────────────────────────
; 哪些异常会让 CPU 多压一个「错误码」？
;
; 压错误码的向量（必须用 ISR_ERRCODE）：8, 10, 11, 12, 13, 14, 17, 21, 29, 30
; 不压的（用 ISR_NOERRCODE）：其余全部
;
; 依据：Intel SDM Vol.3A 6.3.1 / AMD APM Vol.2 8.4.2 的异常表。
;       也可对照 rust-osdev/x86_64 crate 的 IDT 定义——那里用类型区分得很清楚：
;       带 WithErrCode 的正是上面这 10 个。
;
; 注意几个容易搞错的：
;   8  #DF 双重故障     → 压错误码（恒为 0）
;   17 #AC 对齐检查     → 压错误码（恒为 0）；虽然叫"对齐检查"，但它是有错误码的
;   18 #MC 机器检查     → 不压错误码
;   20 #VE 虚拟化异常   → 不压错误码
;   21 #CP 控制保护     → 压错误码
;   28 #HV              → 不压错误码
;   29 #VC VMM 通信     → 压错误码（AMD SEV-ES）
;   30 #SX 安全异常     → 压错误码（Intel）
;
; 搞错的后果：栈上少压/多压 4 字节，regs 结构体整体错位，
; 严重时 iret 会从错误的位置弹出返回地址。
; ─────────────────────────────────────────────────────────────────────────

ISR_NOERRCODE 0    ; 除零
ISR_NOERRCODE 1    ; 调试
ISR_NOERRCODE 2    ; NMI
ISR_NOERRCODE 3    ; 断点
ISR_NOERRCODE 4    ; 溢出
ISR_NOERRCODE 5    ; 越界
ISR_NOERRCODE 6    ; 非法指令
ISR_NOERRCODE 7    ; 协处理器不可用
ISR_ERRCODE   8    ; 双重故障（错误码恒为 0）
ISR_NOERRCODE 9    ; 协处理器段越界（已废弃）
ISR_ERRCODE   10   ; 无效TSS
ISR_ERRCODE   11   ; 段不存在
ISR_ERRCODE   12   ; 栈错误
ISR_ERRCODE   13   ; 通用保护错误
ISR_ERRCODE   14   ; 缺页
ISR_NOERRCODE 15   ; 保留
ISR_NOERRCODE 16   ; x87 浮点异常
ISR_ERRCODE   17   ; 对齐检查（错误码恒为 0）
ISR_NOERRCODE 18   ; 机器检查
ISR_NOERRCODE 19   ; SIMD 浮点异常
ISR_NOERRCODE 20   ; 虚拟化异常 #VE
ISR_ERRCODE   21   ; 控制保护 #CP
ISR_NOERRCODE 22   ; 保留
ISR_NOERRCODE 23   ; 保留
ISR_NOERRCODE 24   ; 保留
ISR_NOERRCODE 25   ; 保留
ISR_NOERRCODE 26   ; 保留
ISR_NOERRCODE 27   ; 保留
ISR_NOERRCODE 28   ; 超级调用注入 #HV
ISR_ERRCODE   29   ; VMM 通信 #VC
ISR_ERRCODE   30   ; 安全异常 #SX
ISR_NOERRCODE 31   ; 保留

; IRQ0~15（硬件中断）：PIC 重映射之后，这些实际对应的中断号是 32~47，
; 不是它们原本的 0~15——跳的仍然是同一个 isr_common_stub，
; C 这边用 int_no 是否 >= 32 来区分"这是异常还是硬件中断"
%macro IRQ 2
global irq%1
irq%1:
    cli
    push dword 0
    push dword %2
    jmp isr_common_stub
%endmacro

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

extern isr_handler

isr_common_stub:
    pusha                    ; 保存全部通用寄存器

    mov ax, ds
    push eax                 ; 保存当前数据段选择子，回去的时候要原样换回来

    mov ax, 0x10              ; 切到内核数据段（对应 GDT 里 DATA_SEG = 0x10）
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                  ; 把现场结构体的地址当参数传给 C 函数
    call isr_handler
    add esp, 4

    pop eax                   ; 换回原来的数据段选择子
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8                ; 跳过之前压的错误码和中断号这两个 dword
    sti
    iret

global idt_flush
idt_flush:
    mov eax, [esp+4]
    lidt [eax]
    ret
