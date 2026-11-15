; userbin.asm —— 把 build/ 里的用户程序二进制直接嵌进内核只读区（v0.16）
; userprog / userfault 由 Makefile 用 nasm -f bin 生成

section .rodata
global userprog_start, userprog_end, userfault_start, userfault_end
align 16
userprog_start:  incbin "build/userprog.bin"
userprog_end:
align 16
userfault_start: incbin "build/userfault.bin"
userfault_end:
