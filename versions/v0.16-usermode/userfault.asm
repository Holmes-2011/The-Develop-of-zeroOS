; userfault.asm —— 故意在 ring 3 里犯错，验证内核会杀掉程序、回到 shell（v0.16）
;
; 先打印一行，再读内核地址 0xC0000000：页表里它是 supervisor 页，
; ring 3 读它会触发 #PF（protection violation），isr_handler 走"用户态故障"分支。

BITS 32
org 0x00400000

start:
    mov eax, 2
    mov ebx, msg
    mov ecx, msg_len
    int 0x80

    mov eax, [0xC0000000]     ; 内核页面：用户态没有权限 → #PF
    jmp $

msg:     db "about to touch kernel memory", 10
msg_len  equ $ - msg
