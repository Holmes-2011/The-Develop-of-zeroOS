; usermode_asm.asm —— 进入 ring 3 和 GDT 重装（v0.16 起；v0.17 起不再需要 user_resume，
; 任务结束走 sched_exit，直接切到别的任务）

BITS 32
global user_enter
global gdt_flush

; void user_enter(unsigned int entry, unsigned int stack) —— 不返回
user_enter:
    push ebp
    mov ebp, esp

    mov ax, 0x23                      ; 用户数据段（0x20 | RPL 3）
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x23                   ; SS
    push dword [ebp + 12]             ; ESP（用户栈顶）
    pushfd
    pop eax
    or eax, 0x200                     ; 开中断
    push eax                          ; EFLAGS
    push dword 0x1B                   ; CS（0x18 | RPL 3）
    push dword [ebp + 8]              ; EIP
    iret

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
