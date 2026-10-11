# zeroOS（零度系统）

zeroOS is an experimental, community-driven operating system project currently in its early bootloader stage (V0.0.1). Built from the ground up with a focus on minimalism and transparency, it invites developers and tech enthusiasts to explore, suggest, and iterate on its core code. Our goal is to evolve this experimental build into a robust, stable release through open collaboration.

zeroOS（零度系统） 是一个处于实验阶段（V0.0.1）的开源操作系统项目。我们从最底层的引导层代码（Bootloader）开始构建，致力于打造一个极简、透明的系统内核。在正式版发布之前，我们向所有开发者开放，欢迎任何人提出建议并参与代码的修改与迭代，共同见证从零到一的突破。

> **项目阶段**：整体处于 **V0.0.1**（早期）；引导层与内核已推进到 **v0.6**。
> 每一版的源码、上游原始压缩包、测试结果和原始串口日志都留档在 [`versions/`](versions/)。

---

# 技术文档 —— x86 引导层 + 最小内核

一个从零写的 32 位 x86 裸机项目：**512 字节引导扇区 + 用 C 写的内核**，
带串口调试输出、IDT 异常处理、PIC 重映射、PIT 时钟中断。

**当前版本：v0.6（PIT 定时器）**
每一版的源码、上游原始压缩包、测试结果和原始串口日志都留档在 [`versions/`](versions/)。

---

## 代码来源与署名

| 部分 | 来源 |
|---|---|
| **v0.1 ~ v0.5 的上游源码** | 由 **Claude Chat** 编写。原始压缩包完整保存在 `versions/v0.3-idt/upstream-source.zip` 和 `versions/v0.5-pic/upstream-source.zip`，未做改动 |
| **v0.5.1 的修正** | 本地修正（共 5 项，详见下方"版本记录"）：17/21/29/30 号异常的错误码分类、引导层补 `cli`、镜像补零兼容 AHCI、macOS 原生构建、测试脚本跨平台 |
| **v0.6 的 PIT 定时器** | 本地新增：`pit.c`/`pit.h`（通道 0，100 Hz）、IRQ0 计数、`elf_to_bin.py` 替代会卡住的 `zig objcopy` |
| **版本档案 / 测试脚本 / 测试记录** | 本地整理 |

> 本项目在 **macOS（Apple Silicon）** 上开发和测试，**没有使用任何 Kali / Linux 虚拟机**。
> 上游 README 曾建议"去 Kali 里编译"，实测没必要 —— 每一步在 macOS 上都有对应替代。

---

## 目录结构

```
README.md                项目说明（就是本文件）
LICENSE                  Apache-2.0
release-notes/           每个版本的 Release 说明文字
.github/workflows/       推送 v* 标签时自动创建 Release
versions/                版本档案：每一版的源码 + 上游原包 + 测试结果 + 串口日志
└── v0.6-pit/            ← 当前版本，要编译就进这个目录
```

**当前版本的源码在 `versions/v0.6-pit/`**，里面这些文件：

```
boot.asm        16 位汇编：BIOS 入口 -> 用磁盘中断加载内核 -> 开 A20 -> 切到 32 位保护模式
entry.asm       32 位汇编，只有几行：关中断 + 把控制权交给 C 函数 kmain()
kernel.c        内核主体：串口调试输出 + 清屏 + 中断/异常分发
idt.c / idt.h   中断描述符表（IDT），256 项，注册 0~47 号门
isr.asm         0~31 号异常 + 0~15 号 IRQ 的入口桩，以及 idt_flush（lidt 指令）
pic.c / pic.h   8259 PIC 重映射：硬件中断号从 0~15 挪到 32~47，避开 CPU 异常号
pit.c / pit.h   8253/8254 PIT 定时器：通道 0 设成 100 Hz，每次 IRQ0 让 tick 加一
elf_to_bin.py   把 kernel.elf 转成纯二进制（替代会卡住的 zig objcopy）
io.h            端口读写公共函数（inb / outb）
link.ld         告诉链接器内核要被放在内存的哪个地址（0x1000）
Makefile        拼出可启动镜像 build/os-image.bin
test-boot.sh    自动化引导测试（macOS / Linux 双平台可用）
```

汇编部分加起来不到 300 行，只做"把 CPU 从 16 位带到 32 位 + 接管中断"这件事，
以后的功能（内存管理、进程、文件系统……）都可以在 `kernel.c` 里用纯 C 写。

## 为什么不能 100% 用 C 写

x86 开机时 CPU 处于 **16 位实模式**，BIOS 只做一件事：把磁盘第一个 512 字节的扇区
（"引导扇区"）加载到内存地址 `0x7c00`，然后直接跳过去执行——此时没有栈约定、
没有 C 运行时初始化、甚至没人替你设好段寄存器。这个阶段 C 编译器帮不上忙，
所以行业里所有引导层（包括 GRUB、Linux 的 boot.S、Windows 的 bootmgr）都是
先用一小段汇编做"开机自检 + 切换到保护模式"，然后再跳进真正的高级语言代码。

## 在哪里编译

**就在这台 Mac 上编译，不需要 Kali 虚拟机、不需要 Homebrew、不需要 sudo。**

macOS 自带的是 clang 而不是 GNU gcc，而且 Apple 的 `ld` 只能生成 Mach-O，
做不出 i386 的 ELF 内核——这两点以前确实很折腾。现在工具链已经配好放在
`~/Documents/Harness WorkSpace/c-dev-apps/toolchain/`，每一步都有对应：

| 原来的做法（Linux） | 这台 Mac 上的替代 |
|---|---|
| `nasm` | 工作区里的 NASM 3.02 |
| `gcc -m32 -ffreestanding` | 系统自带 clang + `--target=i386-unknown-none` |
| `ld -m elf_i386` | Zig 内置的 **LLD**（`zig cc` 驱动） |
| `objcopy -O binary` | `zig objcopy` |
| `qemu-system-i386` | UTM 内置的 QEMU（封装成 `toolchain/qemu-i386`） |

## 编译 + 运行

```bash
cd ~/Desktop/ZERO/zeroOS/versions/v0.6-pit
make        # 生成 build/os-image.bin
make run    # 用 QEMU 启动它
```

如果一切正常，QEMU 窗口会直接变成一片纯黑，没有任何文字、没有图案——
这是故意的：直接开机，不做开机动画/logo 这些"杂七杂八"的东西。

想确认内核是不是真的跑起来了、而不是卡死在某一步，看终端里的串口输出就行——
`make run` 已经加了 `-serial stdio`，正常情况下终端会先打印**四行**，然后**每秒一行** `timer tick`：

```
zeroOS kernel v0.5: entered protected mode, kmain() started
zeroOS kernel v0.5: IDT installed, CPU exceptions now caught
zeroOS kernel v0.6: PIT at 100 Hz, IRQ0 enabled
zeroOS kernel v0.5: screen cleared, entering halt loop
zeroOS timer tick
zeroOS timer tick
...
```

判断方法：

- 一行都没出现 → 没进到 `kmain`，问题在 boot 这边（GDT / 保护模式切换）
- 只出现第一行、没有第二行 → 卡在 `idt_install()` 附近
- 只出现前三行、没有第四行 → 卡在 `clear_screen()` 附近
- 四行都有、但屏幕上有花屏/乱码 → 问题反而不在逻辑上，多半是显存地址算错了

### 画面看不到怎么办：`make shot`

`make run` 的画面是让 macOS 的「屏幕共享」去连 QEMU 的 VNC 端口。如果屏幕共享
连不上（或者你根本不想开窗口），用这个：

```bash
make shot
```

它会**绕开所有 VNC 客户端**：直接让 QEMU 自己把显存渲染成图片，转成 PNG 存到
`build/screen.png`，然后用「预览」打开。同时终端里会打印 CPU 状态、`0xB8000`
的屏幕文字、以及 COM1 串口输出。

### 验证 IDT 真的生效

在 `kernel.c` 的 `kmain()` 里，把那行 `int $0x0` 的注释取消、重新编译运行，
终端会多打印一行：

```
zeroOS kernel: CPU exception -> Division By Zero
```

然后安全停机，而不是像以前那样 QEMU 窗口直接静默重启（没有 IDT 的情况下
触发异常会变成三重故障）。

## 版本记录

- **v0.1** — 直接开机 → 清屏 → 停机，屏幕保持纯黑，无任何画面或文字
- **v0.2** — 新增串口（COM1）调试输出，不影响屏幕观感，仅在终端可见，用于验证内核是否正常运行到哪一步
- **v0.3** — 新增 IDT 异常处理（`idt.c`/`idt.h`/`isr.asm`），捕获 0~31 号 CPU 异常并打印到串口后安全停机，
  不再因为未处理的异常导致虚拟机静默三重故障重启
- **v0.4** — 上游修了两处正确性：① Makefile 补齐/校验 `kernel.bin` 到 `KERNEL_SECTORS` 大小；
  ② `boot.asm` 去掉多余的 `sti`，整个实模式阶段保持中断关闭（**这一版没有收到源码**）
- **v0.5** — 新增 PIC 重映射（`pic.c`/`pic.h`）+ IRQ0~15 入口，把硬件中断号挪到 32~47 避开 CPU 异常号；
  新增 `io.h`；附带上游自带的 `test-boot.sh`
- **v0.5.1** — 本地修正版：修掉 17/21/29/30 号异常的错误码分类；
  引导层补 `cli`；镜像补零到 512 KB 兼容 AHCI；Makefile 改成 macOS 原生；
  `test-boot.sh` 改成跨平台
- **v0.6（当前）** — 新增 PIT 定时器（`pit.c`/`pit.h`）：通道 0 设成 100 Hz，解除 IRQ0 屏蔽并 `sti`，
  每 100 个 tick 往串口打一行 `timer tick`；`elf_to_bin.py` 替代会卡住的 `zig objcopy`；
  `test-boot.sh` 改成必须等到 tick 出现才算通过

## 版本档案：`versions/`

**每个版本的源码 + 上游原始压缩包 + 测试结果，都放在一起。**

```
versions/
├── README.md                     ← 版本总表 + 测试方法说明（先看这个）
├── build-and-test.sh             ← 可复用：重建任意版本并抓串口日志
├── v0.1-hello/                   ← 源码 + 截图 + 测试记录
├── v0.2-serial/
├── v0.3-idt/                     ← 含上游原始 zip（7 个文件）
├── v0.4-nosource/                ← 没收到源码，如实标注
├── v0.5-pic/                     ← 含上游原始 zip（9 个文件）
├── v0.5.1/                   ← v0.5.1 快照 + isr.asm.diff
├── v0.6-pit/                 ← 当前版本
└── experiments/                  ← 不属于版本线的独立实验（图形模式画圆环）
```

每个版本目录里都有一个 **`TEST-RESULTS.md`**，记录：这一版是什么、测了什么、
结果如何、查出了什么缺陷、证据文件在哪。

`serial-output.txt` 是用该版本**自己的源码**重新构建并启动后抓取的原始串口输出，
可以随时用 `./versions/build-and-test.sh <版本目录名>` 复现。

## 兼容性：为什么镜像要补零到 512 KB

`make` 构建出来的 `build/os-image.bin` 实际内容只有 21 KB，但**会被补零到 512 KB**。

原因是磁盘控制器的世代差异：

| 控制器 | QEMU 机型 | 对 21 KB 镜像 |
|---|---|---|
| **IDE / PATA**（1986） | `-M pc`（i440FX + PIIX，1996） | ✅ 接受 |
| **AHCI / SATA**（现代） | `-M q35`（Q35 + ICH9，2009） | ❌ **拒绝启动** |

AHCI 路径下 SeaBIOS 要按容量推算磁盘几何，盘太小会推算失败，于是它认为
"这不是一块可启动的盘"，**连引导扇区都不载入** —— 表现是静默黑屏，一个报错都没有。
实测阈值：≤256 KB 失败，≥512 KB 成功。

**现代真机和现代虚拟机用的都是 AHCI**，所以这不是冷门机型的怪癖，而是真实的可移植性问题。
补进去的全是零，不影响引导层真正读取的那 40 个扇区。

实测（补零后）：

```
pc    成功 15 / 15
q35   成功 15 / 15
```

如果以后想彻底摆脱几何依赖，可以把 `boot.asm` 的 `disk_load` 从 CHS 读（`AH=02h`）
换成 LBA 扩展读（`AH=42h`）—— 但注意：**这一步单独做修不了上面这个启动问题**
（引导扇区压根没被执行，里面写什么都无所谓），它解决的是另一层次的问题。

## 已修复的问题

### 进保护模式前缺 `cli`（v0.3 之后修复）

原来 `boot.asm` 在 `sti` 之后、进保护模式之前没有关中断。此刻 IDT 寄存器里还是
实模式的中断向量表（基址 0、长度 0x3FF），而保护模式会按"描述符"格式解析它 ——
全是垃圾。上电后第一个时钟中断（IRQ0，约每 55ms 一次）一到就会三重故障重启。

现在 `lgdt [gdt_descriptor]` 之后加了一条 `cli`（地址 0x25，正好在 `mov cr0, eax` 之前）：

```
0x20  lgdt word [0x7caf]
0x25  cli                  ← 新增
0x26  mov eax,cr0
0x2D  mov cr0,eax
0x30  jmp word 0x8:word 0x7cb5
```

> 注意：`cli` 只屏蔽可屏蔽中断。CPU 异常（除零、缺页……）不受 IF 影响，
> 所以在 `idt_install()` 建好 IDT 之前，异常仍然会导致三重故障 —— 这是设计如此。

## 下一步可以做什么

- 在 `kernel.c` 里加个简单的 `printf` 风格封装，减少手写 offset 计算
- 给 IDT 补上 32 号以后的中断门（0~31 是 CPU 保留的异常，32 号开始才是硬件中断）
- 建好 IDT 之后在 C 里开中断（`sti`），才能接键盘、时钟这些硬件中断
- 研究 GDT/分页，为后面上 C 写内存管理做准备
- 如果之后想让内核代码更大、更规范，可以考虑切换成 Multiboot + GRUB 的方式，
  这样引导扇区可以直接交给 GRUB，你的汇编代码能进一步减少
- 想让引导层在任意 BIOS 上都稳，可以考虑把读盘改成 LBA 扩展读（见上文"兼容性"）
