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

ISR_NOERRCODE 0    ; 除零
ISR_NOERRCODE 1    ; 调试
ISR_NOERRCODE 2    ; NMI
ISR_NOERRCODE 3    ; 断点
ISR_NOERRCODE 4    ; 溢出
ISR_NOERRCODE 5    ; 越界
ISR_NOERRCODE 6    ; 非法指令
ISR_NOERRCODE 7    ; 协处理器不可用
ISR_ERRCODE   8    ; 双重故障
ISR_NOERRCODE 9
ISR_ERRCODE   10   ; 无效TSS
ISR_ERRCODE   11   ; 段不存在
ISR_ERRCODE   12   ; 栈错误
ISR_ERRCODE   13   ; 通用保护错误
ISR_ERRCODE   14   ; 缺页
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_NOERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

extern fault_handler

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
    call fault_handler
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
