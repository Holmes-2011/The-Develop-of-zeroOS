; userfault.asm —— 故意在 ring 3 里犯错，验证内核会杀掉这个任务、shell 照常活着（v0.17）

BITS 32

start:
    call .here
.here:
    pop ebp
    sub ebp, .here - start

%macro LEA 2
    lea %1, [ebp + (%2 - start)]
%endmacro

    mov eax, 2
    LEA ebx, msg
    mov ecx, msg_len
    int 0x80

    mov eax, [0xC0000000]     ; 内核页面：用户态没有权限 → #PF
    jmp $

msg:     db "about to touch kernel memory", 10
msg_len  equ $ - msg
