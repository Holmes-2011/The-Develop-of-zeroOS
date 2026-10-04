; boot.asm —— 512 字节的 MBR 引导扇区
; 职责：初始化 -> 用 BIOS 中断把内核读入内存 -> 打开 A20 -> 切换到 32 位保护模式 -> 跳进 C 写的内核
; 说明：x86 上电时 CPU 处于 16 位实模式，BIOS 只把这 512 字节加载到 0x7c00 并跳过来执行，
;       这一段没有任何 C 运行环境（没有栈帧约定、没有 libc），所以只能用汇编写。
;       真正的逻辑（打印、以后的内存管理等）会放在 C 里，这里只是"引导"。

BITS 16
ORG 0x7c00

KERNEL_LOAD_ADDR equ 0x1000   ; 内核将被加载到的内存地址
KERNEL_SECTORS   equ 40       ; 从磁盘读取多少个扇区给内核（40*512=20KB）
                               ; v0.3 加了 IDT（256项描述符表约占2KB）后内核变大了一些，提前留够余量

start:
    cli                       ; 初始化阶段先关中断，避免半路被打断
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00            ; 栈从引导扇区往下长
    sti

    mov [boot_drive], dl      ; BIOS 进入时会把启动盘号放在 dl 里，先存起来

    mov bx, KERNEL_LOAD_ADDR
    mov dh, KERNEL_SECTORS
    mov dl, [boot_drive]
    call disk_load             ; 读盘失败会在文字模式下打印错误并停机

    ; 不再切到图形模式画圆环——直接保持 BIOS 默认的文字模式，省去一段开机画面，
    ; 内核那边也相应去掉了画图逻辑，整个流程从加电到接管尽量"直给"，没有中间花活

    call enable_a20
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1                 ; 置 PE 位，开启保护模式
    mov cr0, eax

    jmp CODE_SEG:init_pm      ; 远跳转：刷新 CPU 指令流水线，正式进入 32 位模式

; ---------------- 16 位实模式子程序 ----------------

print_string:
    pusha
    mov ah, 0x0e               ; BIOS 电传打字机功能：一次打一个字符
.next_char:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .next_char
.done:
    popa
    ret

disk_load:
    pusha
    mov [sector_count], dh       ; dh 传进来的是要读的扇区数，先存到内存，
                                 ; 因为下面 dh 要被复用成"磁头号"，两者不能共用寄存器
    mov byte [retry_count], 3

.attempt:
    mov ah, 0x00                 ; 每次读之前先复位一次磁盘系统，真机上更稳
    int 0x13

    mov ah, 0x02                ; BIOS 磁盘读扇区功能
    mov al, [sector_count]      ; 要读的扇区数
    mov ch, 0x00                ; 柱面 0
    mov dh, 0x00                ; 磁头 0
    mov cl, 0x02                ; 从第 2 个扇区开始读（第 1 个扇区是引导扇区自己）
    int 0x13
    jnc .success                ; CF=0 说明读成功，直接返回

    dec byte [retry_count]
    jnz .attempt                ; 还有重试次数就再试一次（BIOS/控制器偶发抖动很常见）

    ; 重试 3 次仍然失败，这才是真正需要报错停机的情况
    mov si, msg_disk_error
    call print_string
    jmp $

.success:
    popa
    ret

enable_a20:
    ; 双保险：先用 BIOS 中断打开 A20，再用键盘控制器快速 A20 方式打开一次，
    ; 两种方式不冲突，哪种在当前机器/模拟器上有效都能生效
    mov ax, 0x2401
    int 0x15

    in al, 0x92
    or al, 2
    out 0x92, al
    ret

boot_drive   db 0
sector_count db 0
retry_count  db 0
msg_disk_error db "Boot: disk read error", 13, 10, 0

; ---------------- GDT（保护模式必须有的段描述表） ----------------
gdt_start:
gdt_null:
    dq 0
gdt_code:
    dw 0xffff
    dw 0
    db 0
    db 10011010b
    db 11001111b
    db 0
gdt_data:
    dw 0xffff
    dw 0
    db 0
    db 10010010b
    db 11001111b
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; ---------------- 32 位保护模式入口 ----------------
BITS 32
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    jmp KERNEL_LOAD_ADDR       ; 跳到刚才用 BIOS 读进来的内核入口，从此交给 C 代码

times 510-($-$$) db 0
dw 0xaa55                      ; 引导扇区签名，缺了这个 BIOS 不认
