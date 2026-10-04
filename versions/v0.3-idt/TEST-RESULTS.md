# v0.3-idt — 测试记录

## 这一版是什么

上游标注的 **v0.3**。新增 **IDT（中断描述符表）+ 0~31 号 CPU 异常处理**。

| 文件 | 大小 | 说明 |
|---|---|---|
| `kernel.c` | 4205 | 串口 + `clear_screen` + `exception_messages[32]` + `fault_handler` + `kmain` |
| `idt.c` | 3667 | 256 项 IDT，`idt_install()` 先把全部清零再逐个注册 0~31 号门 |
| `idt.h` | 655 | `struct idt_entry` / `struct idt_ptr` / 函数声明 |
| `isr.asm` | 2270 | `isr0`~`isr31` 入口桩 + `isr_common_stub` + `idt_flush` |
| `boot.asm` | 4345 | 上游这一版自带的引导层（从 zip 里取出的） |
| `Makefile.linux` | 817 | 上游的 Linux 版 Makefile |
| `README-upstream.md` | 4138 | 上游原版 README |
| `upstream-source.zip` | 10224 | **上游原始压缩包**（完整 7 个文件） |

## 测试项与结果

### ① 基本启动 ✅

```
zeroOS kernel v0.3: entered protected mode, kmain() started
zeroOS kernel v0.3: IDT installed, CPU exceptions now caught
zeroOS kernel v0.3: screen cleared, entering halt loop
```

三行全部出现。

### ② 异常向量抽查 ✅

| 触发 | 期望 | 实际 |
|---|---|---|
| `int $0x00` | Division By Zero | ✅ |
| `int $0x03` | Breakpoint | ✅ |
| `int $0x0D` | General Protection Fault | ✅ |
| `int $0x0E` | Page Fault | ✅ |

> 后来（v0.5 阶段）扩展成了**全部 32 个向量逐个触发**，结果 32/32。
> 见 `v0.5-pic/TEST-RESULTS.md`。

### ③ 稳定性 —— ❌ 第一次扫描发现致命问题

**机型扫描（8 种 QEMU 机型）**：

| 机型 | 结果 |
|---|---|
| `pc` / `pc-i440fx-9.2` / `8.2` / `7.2` / `6.2` / `5.2` / `4.2` | ✅ 7 种全部成功 |
| **`q35`**（Q35 + ICH9，AHCI/SATA） | ❌ **失败** |

**诊断**：`0x7C00` 内存**全零** —— BIOS 根本没把引导扇区载入，内核当然也没读。

**二分定位**（镜像大小 vs 能否启动）：

| 镜像大小 | `pc`（IDE） | `q35`（AHCI） |
|---|---|---|
| 21 KB | ✅ | ❌ |
| 32 / 64 / 128 / 256 KB | — | ❌ |
| **512 KB** | ✅ | ✅ |
| 1 / 4 / 16 MB | ✅ | ✅ |

**根因**：AHCI 路径下 SeaBIOS 要按容量推算磁盘几何，盘太小推算失败，
于是它认为"这不是一块可启动的盘" —— **连引导扇区都不载入**，静默黑屏，一个报错都没有。

**这不是冷门机型的怪癖**：现代真机和现代虚拟机用的都是 AHCI/SATA，
所以是真实的可移植性问题。

### ④ 修复后重测 ✅

修复两处：

1. **镜像补零到 512 KB**（构建时用零填充，不影响引导层读取的 40 个扇区）
2. **引导层 `lgdt` 之后补 `cli`**（关掉时钟中断窗口）

| 测试 | 结果 |
|---|---|
| `pc` 连续启动 15 次 | **15 / 15** |
| `q35` 连续启动 15 次 | **15 / 15** |
| 补零后再扫 8 种机型 | 全部通过 |
| 连续启动累计 | **50 / 50** |

## 这一版暴露的缺陷

| 编号 | 缺陷 | 后果 | 状态 |
|---|---|---|---|
| 1 | 进保护模式前缺 `cli` | 时钟中断 → 三重故障重启 | ✅ 已修 |
| 2 | 21KB 小镜像被 AHCI 拒绝 | q35 / 现代硬件上完全启动不了 | ✅ 已修 |
| 3 | 17/21/29/30 号异常被当成"无错误码" | 当前无害，将来加异常恢复会弹错返回地址 | ⚠️ 到 v0.5.1 才修 |

## 相关背景（值得记下来）

上游 README 建议"去 Kali Linux 虚拟机里编译"，理由是
"macOS 自带的是 clang 不是真正的 gcc，编译裸机代码要装一整套交叉工具链"。

**实测下来这没必要**，macOS 上每一步都有对应：

| 上游 Makefile（Linux） | 这台 Mac 上的替代 |
|---|---|
| `nasm` | NASM 3.02 |
| `gcc -m32 -ffreestanding` | `clang --target=i386-unknown-none -ffreestanding` |
| `ld -m elf_i386` | Zig 内置的 **LLD**（Apple 的 `ld` 只生成 Mach-O） |
| `objcopy -O binary` | `zig objcopy` |
| `qemu-system-i386` | UTM 内置的 QEMU（封装成命令行程序） |

不用装 `gcc-multilib`、不用 sudo、不用开虚拟机。

## 本目录内的证据文件

| 文件 | 说明 |
|---|---|
| `kernel.c / idt.c / idt.h / isr.asm` | 该版本源码 |
| `boot.asm / Makefile.linux / README-upstream.md` | 从上游 zip 里取出 |
| `upstream-source.zip` | **上游原始压缩包**（10224 字节，7 个文件） |
| `serial-output.txt` | 3 行串口输出 |

> `serial-output.txt` 由 `../build-and-test.sh v0.3-idt` 生成 —— 用**该版本自己的源码**重新构建并启动后抓取的原始串口输出，可随时复现。
> 该脚本只编译版本目录里实际存在的模块，不会混入其它版本的代码。
