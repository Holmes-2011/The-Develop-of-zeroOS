; userprog.asm —— 一个 ring 3 演示程序（v0.16）
;
; 被 nasm -f bin 编成裸二进制，由内核复制到 0x00400000 后跳进去执行。
; 它只能通过 int 0x80 和内核打交道：写屏幕、读时间、睡一会儿、调一个工具、退出。
; 最后故意用内核地址调 write，验证内核会拒绝（返回 -1）而不是替它去读内核内存。

BITS 32
org 0x00400000

start:
    ; write("hello from ring 3\n")
    mov eax, 2
    mov ebx, msg_hello
    mov ecx, msg_hello_len
    int 0x80

    ; uptime() -> eax
    mov eax, 4
    int 0x80
    mov esi, eax

    ; write("uptime: ")
    mov eax, 2
    mov ebx, msg_uptime
    mov ecx, msg_uptime_len
    int 0x80

    mov eax, esi
    call print_dec

    ; write(" ms\n")
    mov eax, 2
    mov ebx, msg_ms
    mov ecx, msg_ms_len
    int 0x80

    ; write(0xC0000000, 4) —— 内核地址，必须被拒绝
    mov eax, 2
    mov ebx, 0xC0000000
    mov ecx, 4
    int 0x80
    cmp eax, 0xFFFFFFFF
    jne .guard_bad
    mov eax, 2
    mov ebx, msg_guard_ok
    mov ecx, msg_guard_ok_len
    int 0x80
    jmp .after_guard
.guard_bad:
    mov eax, 2
    mov ebx, msg_guard_bad
    mov ecx, msg_guard_bad_len
    int 0x80
.after_guard:

    ; sleep(50)
    mov eax, 6
    mov ebx, 50
    int 0x80

    ; tool("get_uptime", "", outbuf, 64) -> 结果码
    mov eax, 5
    mov ebx, tool_name
    mov ecx, empty
    mov edx, outbuf
    mov esi, 64
    int 0x80
    mov esi, eax                       ; 结果码存 esi（ebx 后面还要拿来传参数）

    ; write("tool get_uptime -> ")
    mov eax, 2
    mov ebx, msg_tool
    mov ecx, msg_tool_len
    int 0x80

    mov eax, esi
    call print_dec

    ; write(": ")
    mov eax, 2
    mov ebx, msg_colon
    mov ecx, msg_colon_len
    int 0x80

    ; write(outbuf) —— 按 NUL 截断
    xor ecx, ecx
.len_loop:
    cmp byte [outbuf + ecx], 0
    je .len_done
    inc ecx
    cmp ecx, 64
    jb .len_loop
.len_done:
    mov eax, 2
    mov ebx, outbuf
    int 0x80

    ; write("\n")
    mov eax, 2
    mov ebx, msg_nl
    mov ecx, 1
    int 0x80

    ; exit(0)
    mov eax, 1
    xor ebx, ebx
    int 0x80

    jmp $                              ; 不应该到这里

; 打印 eax 的十进制（通过 write 系统调用；int 0x80 会原样恢复所有通用寄存器）
print_dec:
    pushad
    mov ebx, 10
    xor edi, edi
.digit:
    xor edx, edx
    div ebx
    add dl, '0'
    push edx
    inc edi
    test eax, eax
    jnz .digit
.print:
    pop edx
    mov [digitbuf], dl
    mov eax, 2
    mov ebx, digitbuf
    mov ecx, 1
    int 0x80
    dec edi
    jnz .print
    popad
    ret

msg_hello:     db "hello from ring 3", 10
msg_hello_len  equ $ - msg_hello
msg_uptime:    db "uptime: "
msg_uptime_len equ $ - msg_uptime
msg_ms:        db " ms", 10
msg_ms_len     equ $ - msg_ms
msg_guard_ok:  db "guard: kernel address rejected", 10
msg_guard_ok_len equ $ - msg_guard_ok
msg_guard_bad: db "guard: FAILED, kernel address was accepted", 10
msg_guard_bad_len equ $ - msg_guard_bad
msg_tool:      db "tool get_uptime -> "
msg_tool_len   equ $ - msg_tool
msg_colon:     db ": "
msg_colon_len  equ $ - msg_colon
tool_name:     db "get_uptime", 0
empty:         db 0
digitbuf:      db 0
msg_nl:        db 10
outbuf:        times 64 db 0
