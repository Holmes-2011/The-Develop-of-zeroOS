# zeroOS 构建脚本
#
# 目标：在 macOS（Apple Silicon 或 Intel）上，不用 Homebrew、不用 sudo，
#       把 boot.asm + entry.asm + kernel.c + idt.c + isr.asm 编成可引导镜像。
#
# 工具链来源（对应你原来 Linux 版 Makefile 里的每一步）：
#   nasm  ← 工作区里的 NASM 3.02
#   gcc   → 系统自带 Command Line Tools 的 clang（交叉编译到 i386 裸机）
#   ld    → Zig 0.16.0 内置的 LLD（Apple 的 ld 只能生成 Mach-O，做不了 ELF）
#   objcopy → elf_to_bin.py（zig objcopy 曾经卡住，改用这个几十行的脚本）
#   qemu-system-i386 → UTM 内置的 QEMU（见 toolchain/qemu-launcher.c）
#
# v0.6.1 起：找不到上面这套工具链时，自动改用系统里的 nasm / clang / ld.lld /
# qemu-system-i386 —— 这样 Linux 和 GitHub Actions 上也能直接 make。
#
# 常用命令：
#   make              构建 build/os-image.bin
#   make run          真正启动（QEMU + VNC 窗口 + COM1 串口输出）
#   make debug-server 启动 GDB 调试服务（VS Code 按 F5 时会自动调它）
#   make debug-stop   停止调试服务
#   make disasm       反汇编内核
#   make clean        清理

# ---------- 工具链路径 ----------
# `Harness WorkSpace` 里有空格，必须转义成 `\ `；这些变量在 recipe 里不加引号，
# 让 shell 自己去处理那个反斜杠。
# make 的 wildcard 处理不了带空格的路径，所以用 shell 判断工具链在不在。
CDEV       := $(HOME)/Documents/Harness\ WorkSpace/c-dev-apps
HAVE_CDEV  := $(shell [ -x "$(HOME)/Documents/Harness WorkSpace/c-dev-apps/toolchain/zig/zig" ] && echo yes)

ifeq ($(HAVE_CDEV),yes)
NASM       ?= $(CDEV)/toolchain/nasm/nasm
LLD        ?= $(CDEV)/toolchain/zig/zig ld.lld
QEMU_RUN   ?= $(CDEV)/toolchain/qemu-run.sh
QEMU_DEBUG ?= $(CDEV)/toolchain/qemu-debug.sh
QEMU_SHOT  ?= $(CDEV)/toolchain/qemu-shot.sh
else
NASM       ?= nasm
LLD        ?= ld.lld
endif
KCC        ?= clang

# 都可以在命令行覆盖，例如：make NASM=/opt/homebrew/bin/nasm LLD=ld.lld

# ---------- 源文件 ----------
BOOT_ASM   := boot.asm        # 16 位引导扇区
ENTRY_ASM  := entry.asm       # 32 位入口垫片（等价于你 Makefile 里的 kernel_entry.asm）
ISR_ASM    := isr.asm         # 0~31 号异常入口桩 + 32~47 号 IRQ 入口桩
LINKER_LD  := link.ld

# 所有 C 源文件。新增一个 .c 只要加在这里，下面的通用规则会自动编译它
#   kernel   中断分发 + kmain
#   serial   COM1 串口（v0.2）        idt      中断描述符表（v0.3）
#   pic      8259 PIC 重映射（v0.5）   pit      100 Hz 定时器（v0.6）
#   keyboard PS/2 键盘（v0.7）         vga      屏幕文字输出（v0.8）
#   console  屏幕 + 串口同时输出（v0.8） shell    内核命令行（v0.8）
#   pmm      物理页分配（v0.9）         paging   分页（v0.9）
#   heap     kmalloc / kfree（v0.9）    memtest  内存自检命令（v0.9）
C_SRCS     := kernel.c serial.c idt.c pic.c pit.c keyboard.c vga.c console.c shell.c \
              pmm.c paging.c heap.c memtest.c
HEADERS    := $(wildcard *.h)

# ---------- 产物 ----------
BUILD      := build
BOOT_BIN   := $(BUILD)/boot.bin
ENTRY_OBJ  := $(BUILD)/entry.o
ISR_OBJ    := $(BUILD)/isr.o
C_OBJS     := $(C_SRCS:%.c=$(BUILD)/%.o)
KERNEL_OBJS:= $(ENTRY_OBJ) $(ISR_OBJ) $(C_OBJS)   # entry.o 放第一个只是习惯；真正保证它在 0x100000 的是 link.ld
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
IMAGE      := $(BUILD)/os-image.bin

# ---------- 常量（构建时会通过 -D 传给 boot.asm）----------
# v0.9：内核搬到 1MB 以上后不再受 27KB 的限制，先给 128 扇区（64KB），不够再调。
# 上限约 1150（读盘缓冲区 0x10000 起，不能碰到 0x9FC00），boot.asm 里有 %error 检查
KERNEL_SECTORS := 128

# 最终镜像的最小字节数：用零填充到 512 KB。
#
# 为什么需要：镜像本身只有 21 KB 左右，这对 IDE 控制器（QEMU 的 -M pc / i440FX，
# 1996 年的芯片组）完全没问题，但换成 AHCI/SATA 控制器（-M q35 / Q35+ICH9，
# 2009 年）时，SeaBIOS 需要按容量推算磁盘几何，盘太小会推算失败，
# 结果它认为"这不是一块可启动的盘"，连引导扇区都不载入 —— 表现为静默黑屏，
# 一个报错都没有。实测：21KB 在 q35 上必失败，补到 512KB 就能启动。
#
# 填进去的全是零，不占实际数据，也不影响引导层真正读取的那 40 个扇区。
# 现代真机和现代虚拟机都是 AHCI，所以这一步是必要的可移植性处理。
IMAGE_MIN_BYTES := 524288

# ---------- 编译选项 ----------
# --target=i386-unknown-none : 交叉编译到 32 位 x86 裸机（对应 -m32）
# -ffreestanding             : 没有 libc，不假设标准库存在
# -fno-builtin               : 不要自作主张把循环优化成 memset/memcpy
# -fno-stack-protector       : 没有 __stack_chk_fail
# -fno-pic                   : 内核固定在 0x100000（1MB），不需要位置无关代码
# -mno-mmx -mno-sse -mno-sse2 -mno-avx :
#                              不许编译器用 MMX/SSE/AVX 寄存器（v0.9 起）。clang 默认的 i386 目标
#                              是 Pentium 4，-O2 会把循环自动改写成 SSE 向量指令；但内核没有开启 SSE
#                              （CR4.OSFXSR），一执行就是"非法指令"。内核要用浮点/向量时，
#                              还得在每次中断、任务切换时保存这些寄存器——那是以后的事
CFLAGS := --target=i386-unknown-none -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -O2 -Wall -Wextra \
          -mno-mmx -mno-sse -mno-sse2 -mno-avx

# Zig 需要一个可写的全局缓存目录（可通过命令行覆盖）
export ZIG_GLOBAL_CACHE_DIR ?= $(HOME)/.cache/zig

all: $(IMAGE)

$(BUILD):
	@mkdir -p $(BUILD)

# 1. 引导扇区 -> 512 字节纯二进制
#    KERNEL_SECTORS 从这里传给 boot.asm，两边只维护一个数字
$(BOOT_BIN): $(BOOT_ASM) Makefile | $(BUILD)
	$(NASM) -f bin -DKERNEL_SECTORS=$(KERNEL_SECTORS) $(BOOT_ASM) -o "$@"
	@sz=$$(wc -c < "$@" | tr -d ' '); \
	 if [ $$sz -ne 512 ]; then echo "错误：引导扇区是 $$sz 字节，应为 512"; exit 1; fi; \
	 echo "  引导扇区 512 字节  OK"

# 2. 32 位入口垫片
$(ENTRY_OBJ): $(ENTRY_ASM) | $(BUILD)
	$(NASM) -f elf32 "$<" -o "$@"

# 3. 异常 + IRQ 入口桩（isr0~isr31、irq0~irq15、idt_flush）
$(ISR_OBJ): $(ISR_ASM) | $(BUILD)
	$(NASM) -f elf32 "$<" -o "$@"

# 4. 所有 C 文件：一条通用规则。任何头文件改了都全部重编——文件很少，几乎不花时间，
#    换来的是不会因为漏写依赖而用到旧的 .o
$(BUILD)/%.o: %.c $(HEADERS) | $(BUILD)
	$(KCC) $(CFLAGS) -c "$<" -o "$@"

# 5. 链接到 0x100000（直接调 LLD，不再经过 zig cc）
#    链接完再扫一遍：内核里不能出现 MMX/SSE/AVX 指令（原因见上面 CFLAGS 的说明）
$(KERNEL_ELF): $(KERNEL_OBJS) $(LINKER_LD)
	$(LLD) -m elf_i386 -T "$(LINKER_LD)" --build-id=none -o "$@" $(KERNEL_OBJS)
	@if command -v objdump >/dev/null 2>&1; then \
	   n=$$(objdump -d "$@" | grep -cE '%(x|y)mm[0-9]|%mm[0-9]'); \
	   if [ "$$n" -ne 0 ]; then echo "错误：内核里有 $$n 条 MMX/SSE/AVX 指令，CPU 没开启它们，执行到就是非法指令"; rm -f "$@"; exit 1; fi; \
	   echo "  无 MMX/SSE/AVX 指令  OK"; \
	 else echo "  （没有 objdump，跳过 SSE 指令检查）"; fi

# 6. ELF -> 纯二进制，并补齐到引导层要读的扇区数
#    --mem-limit：内核（含 .bss）控制在 4MB 以内（v0.9 以前是 0x7C00）。
#    这不是硬件限制，是给自己定的规矩：内核后面紧跟着物理内存位图，再往后才是能分配的内存，
#    内核真长到这么大时，应该先想想是不是有东西不该放进内核
$(KERNEL_BIN): $(KERNEL_ELF)
	python3 elf_to_bin.py --mem-limit 0x400000 "$<" "$@"
	@sz=$$(wc -c < "$@" | tr -d ' '); want=$$(( $(KERNEL_SECTORS) * 512 )); \
	 if [ $$sz -gt $$want ]; then \
	   echo "错误：内核 $$sz 字节，超过引导层预留的 $$want 字节"; \
	   echo "      请把 boot.asm 里的 KERNEL_SECTORS 调大（当前 $(KERNEL_SECTORS)）"; exit 1; fi; \
	 dd if=/dev/zero bs=1 count=$$((want-sz)) >> "$@" 2>/dev/null; \
	 echo "  内核 $$sz 字节 + 补齐 = $$want 字节  OK"

# 7. 拼成可引导镜像，并补零到 IMAGE_MIN_BYTES
$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	cat $(BOOT_BIN) $(KERNEL_BIN) > "$@"
	@sz=$$(wc -c < "$@" | tr -d ' '); want=$(IMAGE_MIN_BYTES); \
	 if [ "$$sz" -lt "$$want" ]; then \
	   dd if=/dev/zero bs=512 count=$$(( (want - sz + 511) / 512 )) >> "$@" 2>/dev/null; \
	   echo "  补零: $$sz → $$(wc -c < "$@" | tr -d ' ') 字节（为了兼容 AHCI/SATA 控制器，见上方说明）"; \
	 fi
	@echo "==> $(IMAGE)  $$(wc -c < "$@" | tr -d ' ') 字节"

# 用 QEMU 真正启动：BIOS -> 引导层 -> 保护模式 -> C 内核
# 同时把 COM1 串口输出打到终端（kernel.c 里的调试信息就靠它）
# 注意：画面要看「屏幕共享」连 VNC；如果屏幕共享连不上，改用 make shot
run: $(IMAGE)
ifeq ($(HAVE_CDEV),yes)
	$(QEMU_RUN) "$(IMAGE)"
else
	qemu-system-i386 -drive format=raw,file="$(IMAGE)" -serial stdio
endif

# 不需要屏幕共享：直接让 QEMU 把显存渲染成 PNG，用「预览」打开
# 同时打印 CPU 状态、0xB8000 的屏幕文字、COM1 串口输出
shot: $(IMAGE)
	$(QEMU_SHOT) "$(IMAGE)"

# 给 VS Code 的 F5 调试用：启动 QEMU 并冻结 CPU，等 lldb 连上来
debug-server: $(IMAGE)
	$(QEMU_DEBUG) start "$(IMAGE)"

debug-stop:
	$(QEMU_DEBUG) stop

# 反汇编内核 + 打印符号表，确认入口和地址布局
# 注意：Zig 0.16 的 `zig objdump` 只支持 -h，不能反汇编；
#       系统 Command Line Tools 里的 /usr/bin/objdump 是 LLVM 版，能读 ELF。
disasm: $(KERNEL_ELF)
	@echo "── 符号表（_start 应该在 0x100000）──"
	@nm "$(KERNEL_ELF)" | sort | head -30
	@echo
	@echo "── 入口反汇编（0x100000 起）──"
	@objdump -d --start-address=0x100000 --stop-address=0x100010 "$(KERNEL_ELF)" | tail -10

clean:
	rm -rf $(BUILD)

.PHONY: all run shot debug-server debug-stop disasm clean
