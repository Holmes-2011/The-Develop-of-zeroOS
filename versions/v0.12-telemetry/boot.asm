; boot.asm —— 512 字节的 MBR 引导扇区
; 职责：初始化 -> 用 BIOS 把内核读进低端内存 -> 向 BIOS 要内存地图（E820）
;       -> 打开 A20 -> 切到 32 位保护模式 -> 把内核复制到 1MB -> 跳进去
; 说明：x86 上电时 CPU 处于 16 位实模式，BIOS 只把这 512 字节加载到 0x7c00 并跳过来执行，
;       这一段没有任何 C 运行环境（没有栈帧约定、没有 libc），所以只能用汇编写。
;
; v0.9 的变化：
;   * 内核从 0x1000 搬到 1MB（0x100000）。以前内核被夹在 0x1000 和这个引导扇区（0x7C00）
;     之间，最多约 27KB；1MB 以上没有这个限制。
;     但实模式只能访问 1MB 以内，BIOS 读盘也只能读到 1MB 以内——所以先读到 0x10000，
;     进保护模式之后再复制上去。
;   * 读盘改用 LBA 扩展读（int 0x13, AH=0x42），按块读：不再受"柱面/磁头/扇区"几何的限制，
;     以前一次读 40 个扇区在软盘几何（每道 18 扇区）的 BIOS 上会失败。
;   * 向 BIOS 要 E820 内存地图，放在 0x5000，C 内核的物理内存管理靠它知道哪些内存能用。

BITS 16
ORG 0x7c00

%ifndef KERNEL_SECTORS          ; 内核占多少个扇区
%define KERNEL_SECTORS 128      ; Makefile 会用 -DKERNEL_SECTORS=... 传进来，这里只是单独汇编时的默认值
%endif
%if KERNEL_SECTORS > 1150
%error "KERNEL_SECTORS 太大：0x10000 起的读盘缓冲区不能碰到 0x9FC00（EBDA，BIOS 的数据区）"
%endif

LOAD_SEG     equ 0x1000         ; 读盘缓冲区：段 0x1000 = 物理地址 0x10000
KERNEL_PHYS  equ 0x100000       ; 内核最终的位置：1MB（必须和 link.ld 一致）
CHUNK        equ 64             ; 每次读 64 个扇区（32KB）：不超过 BIOS 的 127 上限，也不会跨 64KB 边界
E820_COUNT   equ 0x5000         ; 和 boot_info.h 一致
E820_MAP     equ 0x5004
E820_MAX     equ 64

start:
    cli                       ; 初始化阶段先关中断，避免半路被打断
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00            ; 栈从引导扇区往下长
    sti

    mov [boot_drive], dl      ; BIOS 进入时会把启动盘号放在 dl 里，先存起来

; ---------------- 1. 读内核：LBA 扩展读，按块读到 0x10000 ----------------
    mov bp, KERNEL_SECTORS    ; bp = 还剩多少扇区没读
.read_chunk:
    mov ax, bp
    cmp ax, CHUNK
    jbe .count_ok
    mov ax, CHUNK
.count_ok:
    mov [dap_count], ax
    mov byte [retry_count], 3
.attempt:
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .read_ok
    xor ah, ah                ; 失败：复位磁盘系统再试（真机上偶发抖动很常见）
    int 0x13
    dec byte [retry_count]
    jnz .attempt
    mov si, msg_disk_error    ; 重试 3 次仍然失败，才是真正需要报错停机的情况
    jmp fatal
.read_ok:
    mov ax, [dap_count]
    add [dap_lba], ax         ; 下一块从哪个扇区开始
    adc word [dap_lba + 2], 0
    shl ax, 5                 ; 扇区数 × 512 ÷ 16 = 段地址要往后挪多少
    add [dap_seg], ax
    sub bp, [dap_count]
    jnz .read_chunk

; ---------------- 2. E820 内存地图 ----------------
; 每调用一次 int 0x15 / EAX=0xE820，BIOS 填一条"从哪到哪、什么类型"的记录，
; EBX 是 BIOS 自己的游标，返回 0 表示这是最后一条
    mov di, E820_MAP
    xor ebx, ebx
    xor bp, bp                ; bp = 拿到了几条
.e820_next:
    mov eax, 0xE820
    mov edx, 0x534D4150       ; 'SMAP'：调用约定要求的魔数
    mov ecx, 24
    mov dword [di + 20], 1    ; ACPI 3.0 扩展属性默认"有效"，老 BIOS 只填 20 字节时也不会被误判
    int 0x15
    jc .e820_done             ; CF=1：不支持，或者已经结束
    cmp eax, 0x534D4150
    jne .e820_done
    add di, 24
    inc bp
    test ebx, ebx
    jz .e820_done
    cmp bp, E820_MAX
    jb .e820_next
.e820_done:
    mov [E820_COUNT], bp

; ---------------- 3. A20 + GDT + 进保护模式 ----------------
    call enable_a20
    lgdt [gdt_descriptor]

    ; 进保护模式之前必须把中断关掉：此刻 IDT 寄存器里还是实模式的中断向量表，
    ; 保护模式下按"描述符"格式解析它全是垃圾，第一个时钟中断一到就三重故障。
    ; 等 C 里的 idt_install() 建好自己的 IDT，再按需要重新 sti。
    cli

    mov eax, cr0
    or eax, 1                 ; 置 PE 位，开启保护模式
    mov cr0, eax

    jmp CODE_SEG:init_pm      ; 远跳转：刷新 CPU 指令流水线，正式进入 32 位模式

; ---------------- 16 位实模式子程序 ----------------

fatal:                        ; si = 要打印的错误信息；打印后停机
    mov ah, 0x0e              ; BIOS 电传打字机功能：一次打一个字符
.next_char:
    lodsb
    test al, al
    jz .halt
    int 0x10
    jmp .next_char
.halt:
    hlt
    jmp .halt

enable_a20:
    ; 双保险：先用 BIOS 中断打开 A20，再用"快速 A20"端口 0x92 打开一次，
    ; 两种方式不冲突，哪种在当前机器/模拟器上有效都能生效。
    ; A20 关着的话，访问 1MB 以上会绕回 0 —— 复制内核到 1MB 就会把低端内存写坏
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    or al, 2
    and al, 0xFE              ; 第 0 位写 1 会让 CPU 复位，确保它是 0
    out 0x92, al
    ret

boot_drive   db 0
retry_count  db 0
msg_disk_error db "Boot: disk read error", 13, 10, 0

; LBA 扩展读的参数包（Disk Address Packet）
dap:
    db 0x10                   ; 包长 16 字节
    db 0
dap_count:
    dw 0                      ; 这次读几个扇区
    dw 0                      ; 目标偏移
dap_seg:
    dw LOAD_SEG               ; 目标段
dap_lba:
    dq 1                      ; 从第 1 个扇区开始（第 0 个是引导扇区自己）

; ---------------- GDT（保护模式必须有的段描述表） ----------------
; 这张表只在引导阶段用；内核在 entry.asm 里会换上自己的 GDT，
; 之后这个扇区所在的内存就可以不管了
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
    mov esp, 0x7c00           ; 临时栈（内核在 entry.asm 里会换成自己的）

    ; 把内核从 0x10000 复制到 1MB
    cld
    mov esi, LOAD_SEG * 16
    mov edi, KERNEL_PHYS
    mov ecx, KERNEL_SECTORS * 512 / 4
    rep movsd

    mov eax, KERNEL_PHYS
    jmp eax                   ; 跳进内核，从此交给 C 代码

times 510-($-$$) db 0
dw 0xaa55                      ; 引导扇区签名，缺了这个 BIOS 不认
