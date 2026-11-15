; userbin.asm —— 把 build/ 里的用户程序二进制直接嵌进内核只读区（v0.16 起，v0.17 加 loop）

section .rodata
global userprog_start, userprog_end, userfault_start, userfault_end, userloop_start, userloop_end
align 16
userprog_start:  incbin "build/userprog.bin"
userprog_end:
align 16
userfault_start: incbin "build/userfault.bin"
userfault_end:
align 16
userloop_start:  incbin "build/userloop.bin"
userloop_end:
