; boot.asm —— 512 字节的 MBR 引导扇区
; 职责：初始化 -> 用 BIOS 中断把内核读入内存 -> 打开 A20 -> 切换到 32 位保护模式 -> 跳进 C 写的内核
; 说明：x86 上电时 CPU 处于 16 位实模式，BIOS 只把这 512 字节加载到 0x7c00 并跳过来执行，
;       这一段没有任何 C 运行环境（没有栈帧约定、没有 libc），所以只能用汇编写。
;       真正的逻辑（打印、以后的内存管理等）会放在 C 里，这里只是"引导"。

BITS 16
ORG 0x7c00

KERNEL_LOAD_ADDR equ 0x1000   ; 内核将被加载到的内存地址
KERNEL_SECTORS   equ 20       ; 从磁盘读取多少个扇区给内核（20*512=10KB，够用了，不够再改大）

start:
    cli                       ; 初始化阶段先关中断，避免半路被打断
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00            ; 栈从引导扇区往下长
    sti

    mov [boot_drive], dl      ; BIOS 进入时会把启动盘号放在 dl 里，先存起来

    mov si, msg_real_mode
    call print_string

    mov bx, KERNEL_LOAD_ADDR
    mov dh, KERNEL_SECTORS
    mov dl, [boot_drive]
    call disk_load

    ; ---- 显示模式开关 ------------------------------------------------
    ; 默认走 BIOS 文本模式（80x25，显存 0xB8000）—— kernel.c（串口调试版）
    ; 用这个模式：它往 0xB8000 写空格 + 属性 0x00，屏幕呈纯黑。
    ;
    ; 如果要跑 kernel-ring.c（图形模式画圆环），把下面两行取消注释：
    ; 切到 VGA 模式 0x13 必须在实模式下做，因为切显卡模式是 BIOS 的服务
    ; （int 0x10, ah=0x00）；进了保护模式就没有 BIOS 中断可用了。
    ; 切完之后显存变成从 0xA0000 开始的一整块线性缓冲区，每像素一字节，
    ; 字节的值是调色板编号（0 黑、15 白）。
    ;
    ; mov ax, 0x0013
    ; int 0x10
    ; -----------------------------------------------------------------

    call enable_a20
    lgdt [gdt_descriptor]

    cli                       ; 进保护模式前彻底关中断：IDT 还没建立，
                              ; 此时来一个时钟中断就是三重故障重启

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
    mov ah, 0x00                 ; 先复位一次磁盘系统，真机上第一次读盘更稳
    int 0x13
    mov ah, 0x02                ; BIOS 磁盘读扇区功能
    mov al, dh                  ; 要读的扇区数
    mov ch, 0x00                ; 柱面 0
    mov dh, 0x00                ; 磁头 0
    mov cl, 0x02                ; 从第 2 个扇区开始读（第 1 个扇区是引导扇区自己）
    int 0x13
    jc disk_error
    popa
    ret
disk_error:
    mov si, msg_disk_error
    call print_string
    jmp $

enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

boot_drive db 0
msg_real_mode db "Boot: real mode ok, loading kernel...", 13, 10, 0
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
