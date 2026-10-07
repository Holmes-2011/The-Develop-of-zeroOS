# zeroOS 构建脚本
#
# 目标：在 macOS（Apple Silicon 或 Intel）上，不用 Homebrew、不用 sudo，
#       把 boot.asm + entry.asm + kernel.c + idt.c + isr.asm 编成可引导镜像。
#
# 工具链来源（对应你原来 Linux 版 Makefile 里的每一步）：
#   nasm  ← 工作区里的 NASM 3.02
#   gcc   → 系统自带 Command Line Tools 的 clang（交叉编译到 i386 裸机）
#   ld    → Zig 0.16.0 内置的 LLD（Apple 的 ld 只能生成 Mach-O，做不了 ELF）
#   objcopy → zig objcopy
#   qemu-system-i386 → UTM 内置的 QEMU（见 toolchain/qemu-launcher.c）
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
CDEV       := $(HOME)/Documents/Harness\ WorkSpace/c-dev-apps
ZIG        := $(CDEV)/toolchain/zig/zig
NASM       := $(CDEV)/toolchain/nasm/nasm
QEMU_RUN   := $(CDEV)/toolchain/qemu-run.sh
QEMU_DEBUG := $(CDEV)/toolchain/qemu-debug.sh
QEMU_SHOT  := $(CDEV)/toolchain/qemu-shot.sh

# 如果以后用 Homebrew 装了工具链，把上面几行换成无空格路径即可：
#   ZIG  := /opt/homebrew/bin/zig
#   NASM := /opt/homebrew/bin/nasm

# ---------- 源文件 ----------
BOOT_ASM   := boot.asm        # 16 位引导扇区
ENTRY_ASM  := entry.asm       # 32 位入口垫片（等价于你 Makefile 里的 kernel_entry.asm）
KERNEL_C   := kernel.c
IDT_C      := idt.c
PIC_C      := pic.c           # v0.5：8259 PIC 重映射
PIT_C      := pit.c
ISR_ASM    := isr.asm         # 0~31 号异常入口桩 + 32~47 号 IRQ 入口桩
IDT_H      := idt.h
PIC_H      := pic.h
IO_H       := io.h            # v0.5：端口读写公共函数
LINKER_LD  := link.ld

# ---------- 产物 ----------
BUILD      := build
BOOT_BIN   := $(BUILD)/boot.bin
ENTRY_OBJ  := $(BUILD)/entry.o
KERNEL_OBJ := $(BUILD)/kernel.o
IDT_OBJ    := $(BUILD)/idt.o
PIC_OBJ    := $(BUILD)/pic.o
PIT_OBJ    := $(BUILD)/pit.o
ISR_OBJ    := $(BUILD)/isr.o
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
IMAGE      := $(BUILD)/os-image.bin

# ---------- 常量（必须与 boot.asm 里的 KERNEL_SECTORS 一致）----------
# v0.3 加了 IDT（256 项 × 8 字节 ≈ 2KB）后引导层把它提到了 40 扇区（20KB）
KERNEL_SECTORS := 40

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
# -fno-pic                   : 内核固定在 0x1000，不需要位置无关代码
CFLAGS := --target=i386-unknown-none -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -O2 -Wall -Wextra

# Zig 需要一个可写的全局缓存目录（可通过命令行覆盖）
export ZIG_GLOBAL_CACHE_DIR ?= $(HOME)/.cache/zig

all: $(IMAGE)

$(BUILD):
	@mkdir -p $(BUILD)

# 1. 引导扇区 -> 512 字节纯二进制
$(BOOT_BIN): $(BOOT_ASM) | $(BUILD)
	$(NASM) -f bin $(BOOT_ASM) -o "$@"
	@sz=$$(stat -f%z "$@"); \
	 if [ $$sz -ne 512 ]; then echo "错误：引导扇区是 $$sz 字节，应为 512"; exit 1; fi; \
	 echo "  引导扇区 512 字节  OK"

# 2. 32 位入口垫片
$(ENTRY_OBJ): $(ENTRY_ASM) | $(BUILD)
	$(NASM) -f elf32 "$<" -o "$@"

# 3. 异常 + IRQ 入口桩（isr0~isr31、irq0~irq15、idt_flush）
$(ISR_OBJ): $(ISR_ASM) | $(BUILD)
	$(NASM) -f elf32 "$<" -o "$@"

# 4. C 内核
$(KERNEL_OBJ): $(KERNEL_C) $(IDT_H) $(PIC_H) $(IO_H) pit.h | $(BUILD)
	clang $(CFLAGS) -c "$<" -o "$@"

# 5. IDT 实现
$(IDT_OBJ): $(IDT_C) $(IDT_H) | $(BUILD)
	clang $(CFLAGS) -c "$<" -o "$@"

# 6. PIC 重映射（v0.5）
$(PIC_OBJ): $(PIC_C) $(PIC_H) $(IO_H) | $(BUILD)
	clang $(CFLAGS) -c "$<" -o "$@"

$(PIT_OBJ): $(PIT_C) pit.h $(IO_H) | $(BUILD)
	clang $(CFLAGS) -c "$<" -o "$@"

# 7. 链接到 0x1000（用 zig 自带的 LLD）
$(KERNEL_ELF): $(ENTRY_OBJ) $(IDT_OBJ) $(PIC_OBJ) $(PIT_OBJ) $(ISR_OBJ) $(KERNEL_OBJ) $(LINKER_LD)
	$(ZIG) cc --target=x86-freestanding -nostdlib -fno-sanitize=all \
	    -Wl,-T,"$(LINKER_LD)" -Wl,--build-id=none \
	    -o "$@" $(ENTRY_OBJ) $(IDT_OBJ) $(PIC_OBJ) $(PIT_OBJ) $(ISR_OBJ) $(KERNEL_OBJ)

# 7. ELF -> 纯二进制，并补齐到引导层要读的扇区数
$(KERNEL_BIN): $(KERNEL_ELF)
	python3 elf_to_bin.py "$<" "$@"
	@sz=$$(stat -f%z "$@"); want=$$(( $(KERNEL_SECTORS) * 512 )); \
	 if [ $$sz -gt $$want ]; then \
	   echo "错误：内核 $$sz 字节，超过引导层预留的 $$want 字节"; \
	   echo "      请把 boot.asm 里的 KERNEL_SECTORS 调大（当前 $(KERNEL_SECTORS)）"; exit 1; fi; \
	 dd if=/dev/zero bs=1 count=$$((want-sz)) >> "$@" 2>/dev/null; \
	 echo "  内核 $$sz 字节 + 补齐 = $$want 字节  OK"

# 8. 拼成可引导镜像，并补零到 IMAGE_MIN_BYTES
$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	cat $(BOOT_BIN) $(KERNEL_BIN) > "$@"
	@sz=$$(stat -f%z "$@"); want=$(IMAGE_MIN_BYTES); \
	 if [ "$$sz" -lt "$$want" ]; then \
	   dd if=/dev/zero bs=512 count=$$(( (want - sz + 511) / 512 )) >> "$@" 2>/dev/null; \
	   echo "  补零: $$sz → $$(stat -f%z "$@") 字节（为了兼容 AHCI/SATA 控制器，见上方说明）"; \
	 fi
	@echo "==> $(IMAGE)  $$(stat -f%z "$@") 字节"

# 用 QEMU 真正启动：BIOS -> 引导层 -> 保护模式 -> C 内核
# 同时把 COM1 串口输出打到终端（kernel.c 里的调试信息就靠它）
# 注意：画面要看「屏幕共享」连 VNC；如果屏幕共享连不上，改用 make shot
run: $(IMAGE)
	$(QEMU_RUN) "$(IMAGE)"

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
	@echo "── 符号表（_start 应该在 0x1000）──"
	@nm "$(KERNEL_ELF)" | sort | head -30
	@echo
	@echo "── 入口反汇编（0x1000 起）──"
	@objdump -d --start-address=0x1000 --stop-address=0x1010 "$(KERNEL_ELF)" | tail -10

clean:
	rm -rf $(BUILD)

.PHONY: all run shot debug-server debug-stop disasm clean
