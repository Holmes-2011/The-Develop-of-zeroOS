; userprog.asm —— ring 3 演示程序（v0.17：位置无关）
;
; v0.17 起每个任务被放在不同的 16MB 窗口里（4MB、20MB、36MB……），
; 所以程序不能再用 org 0x400000 的绝对地址：开头用 call/pop 拿到自己的加载基址，
; 之后所有数据引用都写成 [基址 + 偏移]。

BITS 32

start:
    call .here
.here:
    pop ebp
    sub ebp, .here - start          ; ebp = 本程序被加载到的基址

%macro LEA 2
    lea %1, [ebp + (%2 - start)]
%endmacro

    ; write("hello from ring 3\n")
    mov eax, 2
    LEA ebx, msg_hello
    mov ecx, msg_hello_len
    int 0x80

    ; uptime() -> eax
    mov eax, 4
    int 0x80
    mov esi, eax

    ; write("uptime: ")
    mov eax, 2
    LEA ebx, msg_uptime
    mov ecx, msg_uptime_len
    int 0x80

    mov eax, esi
    call print_dec

    ; write(" ms\n")
    mov eax, 2
    LEA ebx, msg_ms
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
    LEA ebx, msg_guard_ok
    mov ecx, msg_guard_ok_len
    int 0x80
    jmp .after_guard
.guard_bad:
    mov eax, 2
    LEA ebx, msg_guard_bad
    mov ecx, msg_guard_bad_len
    int 0x80
.after_guard:

    ; sleep(50)
    mov eax, 6
    mov ebx, 50
    int 0x80

    ; tool("get_uptime", "", outbuf, 64) -> 结果码
    mov eax, 5
    LEA ebx, tool_name
    LEA ecx, empty
    LEA edx, outbuf
    mov esi, 64
    int 0x80
    mov edi, eax                   ; 结果码存 edi（ebx 后面还要传参）

    ; write("tool get_uptime -> ")
    mov eax, 2
    LEA ebx, msg_tool
    mov ecx, msg_tool_len
    int 0x80

    mov eax, edi
    call print_dec

    ; write(": ")
    mov eax, 2
    LEA ebx, msg_colon
    mov ecx, msg_colon_len
    int 0x80

    ; write(outbuf) —— 按 NUL 截断
    LEA esi, outbuf
    xor ecx, ecx
.len_loop:
    cmp byte [esi + ecx], 0
    je .len_done
    inc ecx
    cmp ecx, 64
    jb .len_loop
.len_done:
    mov eax, 2
    mov ebx, esi
    int 0x80

    ; write("\n")
    mov eax, 2
    LEA ebx, msg_nl
    mov ecx, 1
    int 0x80

    ; exit(0)
    mov eax, 1
    xor ebx, ebx
    int 0x80

    jmp $                          ; 不应该到这里

; 打印 eax 的十进制（int 0x80 会原样恢复所有通用寄存器，ebp 也保住）
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
    LEA ebx, digitbuf
    mov [ebx], dl
    mov eax, 2
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
msg_nl:        db 10
tool_name:     db "get_uptime", 0
empty:         db 0
digitbuf:      db 0
outbuf:        times 64 db 0
