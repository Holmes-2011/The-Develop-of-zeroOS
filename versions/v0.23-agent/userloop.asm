; userloop.asm —— 后台任务：每 100ms 打一行 tick，共 10 行（v0.17）
; 用来演示"程序在后台跑、命令行照样能用"：跑的时候敲 echo 会立刻有响应。

BITS 32

start:
    call .here
.here:
    pop ebp
    sub ebp, .here - start

%macro LEA 2
    lea %1, [ebp + (%2 - start)]
%endmacro

    mov edi, 20
.loop:
    mov eax, 2
    LEA ebx, msg_tick
    mov ecx, msg_tick_len
    int 0x80

    mov eax, 6
    mov ebx, 100
    int 0x80

    dec edi
    jnz .loop

    mov eax, 1
    xor ebx, ebx
    int 0x80
    jmp $

msg_tick:     db "tick", 10
msg_tick_len  equ $ - msg_tick
