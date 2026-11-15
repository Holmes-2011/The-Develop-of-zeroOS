; usermode_asm.asm —— ring 3 的进出（v0.16）
;
; user_enter(entry, stack)：保存内核现场，搭一个 iret 帧，切到 ring 3。
; user_resume()：从异常 / 系统调用回到 user_enter 的调用者（把内核现场恢复回去）。
; gdt_flush(gdt_ptr*)：lgdt + 重装段寄存器（gdt.c 用）。

BITS 32

global user_enter
global user_resume
global gdt_flush
extern kernel_resume_esp

; void user_enter(unsigned int entry, unsigned int stack)
user_enter:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov [kernel_resume_esp], esp      ; 记住这里：user_resume 从这里恢复

    mov ax, 0x23                      ; 用户数据段（0x20 | RPL 3）
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x23                   ; SS
    push dword [ebp + 12]             ; ESP（用户栈顶）
    pushfd
    pop eax
    or eax, 0x200                     ; 进 ring 3 时开中断：时钟、键盘照常工作
    push eax                          ; EFLAGS
    push dword 0x1B                   ; CS（0x18 | RPL 3）
    push dword [ebp + 8]              ; EIP（程序入口）
    iret

; void user_resume(void) —— 不返回
user_resume:
    mov esp, [kernel_resume_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; void gdt_flush(struct gdt_ptr *p)
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload
.reload:
    ret
