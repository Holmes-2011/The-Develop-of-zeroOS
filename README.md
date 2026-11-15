# zeroOS（零度系统）

zeroOS is an experimental, community-driven operating system project currently in its early bootloader stage (V0.0.1). Built from the ground up with a focus on minimalism and transparency, it invites developers and tech enthusiasts to explore, suggest, and iterate on its core code. Our goal is to evolve this experimental build into a robust, stable release through open collaboration.

zeroOS（零度系统） 是一个处于实验阶段（V0.0.1）的开源操作系统项目。我们从最底层的引导层代码（Bootloader）开始构建，致力于打造一个极简、透明的系统内核。在正式版发布之前，我们向所有开发者开放，欢迎任何人提出建议并参与代码的修改与迭代，共同见证从零到一的突破。

> **项目阶段**：整体处于 **V0.0.1**（早期）；引导层与内核已推进到 **v0.23**。
> 每一版的源码、上游原始压缩包、测试结果和原始串口日志都留档在 [`versions/`](versions/)。
>
> **Wiki**：每个子系统的实现详解、测试方法与路线图 → <https://github.com/Holmes-2011/The-Develop-of-zeroOS/wiki>

---

# 技术文档 —— x86 引导层 + 最小内核

一个从零写的 32 位 x86 裸机项目：**512 字节引导扇区 + 用 C 写的内核**，
带串口调试输出、IDT 异常处理、PIC 重映射、PIT 时钟中断、PS/2 键盘输入、屏幕文字输出、内核命令行，内存管理（物理页分配、分页、内核堆）、浮点 / SSE2 和数学函数、ATA 硬盘驱动，以及感知层（计数器、采样、事件日志）。

**当前版本：v0.23（宿主侧 Agent 循环接进来）**

长期方向：把项目自己训练的模型和 Agent 整个放进 zeroOS 本机运行。设计与路线图见 [`docs/agent-design.md`](docs/agent-design.md)。
每一版的源码、上游原始压缩包、测试结果和原始串口日志都留档在 [`versions/`](versions/)。

---

## 代码来源与署名

| 部分 | 来源 |
|---|---|
| **v0.1 ~ v0.5 的上游源码** | 由 **Claude Chat** 编写。原始压缩包完整保存在 `versions/v0.3-idt/upstream-source.zip` 和 `versions/v0.5-pic/upstream-source.zip`，未做改动 |
| **v0.5.1 的修正** | 本地修正（共 5 项，详见下方"版本记录"）：17/21/29/30 号异常的错误码分类、引导层补 `cli`、镜像补零兼容 AHCI、macOS 原生构建、测试脚本跨平台 |
| **v0.6 的 PIT 定时器** | 本地新增：`pit.c`/`pit.h`（通道 0，100 Hz）、IRQ0 计数、`elf_to_bin.py` 替代会卡住的 `zig objcopy` |
| **v0.6.1 的底层修正** | 本地修正：内存上限检查、`.bss` 清零、中断入口 `cld`、伪中断处理、可移植构建 + GitHub Actions |
| **v0.7 的键盘驱动** | 本地新增：`keyboard.c`/`keyboard.h`（IRQ1、扫描码第 1 套、Shift / Caps Lock）、行输入回显、`sendkey` 自动按键测试 |
| **v0.8 的屏幕和命令行** | 本地新增：`vga.c`（80×25 文字模式、滚屏、光标）、`console.c`（屏幕 + 串口）、`shell.c`（`help` / `clear` / `echo` / `uptime` / `mem` / `version`）、显存快照测试 |
| **v0.9 的内存管理** | 本地新增：引导扇区改 LBA 读盘 + E820 + 内核搬到 1MB；`pmm.c`（物理页）、`paging.c`（分页，第 0 页不映射）、`heap.c`（`kmalloc` / `kfree`）、`memtest` 自检 |
| **v0.10 的浮点** | 本地新增：`cpu.c`（CPUID、开启 FPU/SSE2）、`kmath.c`（数学函数）、`kstring.c`、`mathtest` / `bench`；核心代码与浮点代码分开编译 |
| **v0.10.1 的修正** | 本地修正：`time.c`（开机时间改用 TSC，满负荷时不再变慢）、堆释放时归还末尾多余的页 |
| **v0.11 的硬盘驱动** | 本地新增：`blockdev.c`（块设备接口）、`ata.c`（ATA PIO，支持 LBA48）、`disks` / `diskread` / `diskcheck` / `diskbench`、`gen_testdisk.py` |
| **v0.13 的工具层** | 本地新增：`tools.c`（工具登记、4 级权限、审计）、`confirm.c`（内核绘制的确认框，只认物理键盘）、`builtin_tools.c`（11 个工具，含诊断和故障注入）、`tools` / `tool` / `devmode` / `audit` / `sleep` |
| **v0.12 的感知层** | 本地新增：`telemetry.c`（计数器、100 毫秒采样、事件日志）、`demand.c`（按需分页区）、`metrics` / `events` / `telstream` |
| **v0.14 的桥接协议** | 本地新增：`serial.c` 接收、`kjson.c`、`bridge.c`（hello / call / result / event / verdict）、`get_intuition`、`bridge_test.py` |
| **v0.15 的高半内核** | 本地新增：`link.ld` 链接到 `0xC0100000`（加载地址仍是 1MB）、`entry.asm` 临时页表 + 跳到高地址、`paging.c` 的 `P2V` / 768MB 直接映射窗口；堆挪到 `0xF0000000`、按需分页区挪到 `0xF8000000`；`memtest` 26 项 |
| **v0.15.1 的关机** | 本地新增：`shutdown` 命令写 ACPI PM1a_CNT（`0x604`）让宿主机断电，QEMU 的 `pc` / `q35` 都能关；关不掉时退化成停机并说明。`outw` 加进 `io.h` |
| **v0.16 的用户态** | 本地新增：`gdt.c`（ring 3 段 + TSS）、`usermode.c`（用户页、`int 0x80`、故障恢复）、`userprog.asm` / `userfault.asm`（内嵌程序）、`run` 命令 |
| **v0.17 的调度器** | 本地新增：`sched.c`（任务表、轮转、抢占、僵尸回收、FXSAVE）、`sched_asm.asm`（`switch_to`）、`userloop.asm`（后台 tick 任务）、`jobs` / `kill` |
| **v0.18 的图形模式** | 本地新增：boot 阶段 VBE 1024×768×32、`gfx.c`（LFB 映射、文字控制台、矩形 / 圆 / 渐变）、`gfx` 命令；`docs/images/v0.18-gfx.png` |
| **v0.19 的桌面** | 本地新增：`mouse.c`（PS/2 鼠标 IRQ12）、`wm.c`（两个窗口、指针、拖动、焦点）、开机背景、`desktop` / `wminfo`、`docs/images/v0.19-desktop.png` |
| **v0.20 的 USB 平板** | 本地新增：`pci.c`（配置空间）、`usb.c`（UHCI + 枚举 + HID 平板）、绝对指针优先、`docs/images/v0.20-usb-mouse.png` |
| **v0.20.1 的合成桌面** | 本地新增：`gfx.c` 双缓冲 / 裁剪 / 脏区 present；`wm.c` 窗口阴影、标题栏、任务栏、Exit 按钮、60 秒空闲退出；`docs/images/v0.20.1-desktop.png` |
| **v0.20.2 的桌面自启** | `AUTO_DESKTOP=1` 编译开关：开机直接 `wm_run()`，ESC / Exit 回 shell；`docs/images/v0.20.2-autodesktop.png` |
| **v0.21 的联网** | 本地新增：`virtio_net.c`（legacy virtio-net）、`net.c`（ARP/IPv4/ICMP/UDP/DNS）、`net` / `ping` / `dns` 命令 |
| **v0.22.1 的顺滑拖动** | 拖动时预渲染“无窗场景 + 窗口精灵”，移动只做 `memcpy`；透明文字；`docs/images/v0.22.1-drag.png` |
| **v0.23 的 Agent 循环** | 宿主侧 `agent/`：协议 v1 客户端、scripted / OpenAI 兼容后端、桌面模式轮询桥接；`docs/images/v0.23-agent.png` |
| **版本档案 / 测试脚本 / 测试记录** | 本地整理 |

## 作者与协作者

- **项目作者**：Holmes-2011（Zero Singularity）
- **AI 协作者**：DeepSeek Harness（deepseek-flash）、Claude（Anthropic）
- 完整名单见 [`AUTHORS`](AUTHORS)

> 本项目在 **macOS（Apple Silicon）** 上开发和测试，**没有使用任何 Kali / Linux 虚拟机**。
> 上游 README 曾建议"去 Kali 里编译"，实测没必要 —— 每一步在 macOS 上都有对应替代。

---

## 目录结构

```
README.md                项目说明（就是本文件）
AUTHORS                  作者与协作者名单
LICENSE                  Apache-2.0
release-notes/           每个版本的 Release 说明文字
.github/workflows/       推送 v* 标签时自动创建 Release；每次推送自动编译 + QEMU 启动测试
docs/                    设计文档（系统内置 Agent 的设计与路线图）
versions/                版本档案：每一版的源码 + 上游原包 + 测试结果 + 串口日志
└── v0.23-agent/        ← 当前版本，要编译就进这个目录
```

**当前版本的源码在 `versions/v0.23-agent/`**，里面这些文件：

```
boot.asm        16 位汇编：BIOS 入口 -> LBA 读盘 -> 要 E820 内存地图 -> 开 A20 -> 保护模式 -> 把内核复制到 1MB
entry.asm       32 位汇编，只有几行：关中断 + 把控制权交给 C 函数 kmain()
kernel.c        中断/异常分发 + 内核入口 kmain()
serial.c / .h   COM1 串口输出（调试日志、自动测试都靠它）
idt.c / idt.h   中断描述符表（IDT），256 项，注册 0~47 号门
isr.asm         0~31 号异常 + 0~15 号 IRQ 的入口桩，以及 idt_flush（lidt 指令）
pic.c / pic.h   8259 PIC 重映射：硬件中断号从 0~15 挪到 32~47，避开 CPU 异常号
pit.c / pit.h   8253/8254 PIT 定时器：通道 0 设成 100 Hz，每次 IRQ0 让 tick 加一
keyboard.c / .h PS/2 键盘驱动：IRQ1 读扫描码 → 翻译成字符 → 环形缓冲区
vga.c / vga.h   屏幕文字输出：80×25、16 色、硬件光标、写满自动上滚
console.c / .h  同一份文字同时写屏幕和串口
shell.c / .h    内核命令行：读一行 → 查命令表执行
pmm.c / .h      物理页分配：按 E820 地图，用位图管理每一页 4KB
paging.c / .h   分页：内核高半区 + 物理内存直接映射窗口（768MB），低 3GB 不映射（抓空指针）
heap.c / .h     内核堆 kmalloc / kfree（0xF0000000 起，按需映射）
memtest.c / .h  内存自检（memtest 命令）
telemetry.c / .h 感知层：各子系统的计数器、每 100 毫秒采样一次、事件日志（v0.12）
demand.c / .h   按需分页区：第一次访问时才分配物理页，缺页不再停机（v0.12）
telcmd.c / .h   metrics / events / telstream 命令（v0.12）
tools.c / .h    工具层：工具登记、权限判定、开发者模式、审计日志（v0.13）
confirm.c / .h  内核绘制的确认框：只认物理键盘的 y / n，弹框前清空键盘缓冲区（v0.13）
builtin_tools.c 内置工具：查询、诊断（heap_check / pmm_check）、故障注入（v0.13）
toolcmd.c / .h  tools / tool / devmode / audit 命令（v0.13）
stack.c / .h    内核栈水位和溢出检测（v0.13.2）
kjson.c / .h    协议用的最小 JSON 子集（v0.14）
bridge.c / .h   串口桥接协议：宿主机 Agent 的 hello / call / verdict，内核的 welcome / result / event（v0.14）
bridge_test.py  扮演宿主机走一遍协议（test-boot.sh 第二阶段自动跑）
boot_info.h     引导扇区留给内核的信息（E820 地图的位置）
cpu.c / .h      CPUID；支持的话开启 FPU + SSE2
time.c / .h     开机时间：用 TSC 计时（开机时校准），没有 TSC 时数时钟中断
blockdev.c / .h 块设备接口：所有硬盘驱动的统一入口
ata.c / .h      ATA（IDE）硬盘驱动，PIO 方式，支持 LBA48
disktest.c / .h disks / diskread / diskcheck / diskbench 命令
gen_testdisk.py 生成测试硬盘（每个扇区的内容都能按扇区号校验）
kstring.c / .h  memset / memcpy 等基本函数
kmath.c / .h    数学函数（sqrtf、expf、logf、sinf……），用 SSE2 编译
floattest.c     mathtest 命令；bench.c：bench 命令；simd_test.asm：验证中断不破坏 XMM
mathref.h       数学函数的参考值（由 gen_mathref.py 生成）
elf_to_bin.py   把 kernel.elf 转成纯二进制（替代会卡住的 zig objcopy）
io.h            端口读写公共函数（inb / outb）
link.ld         内核链接到 0xC0100000（高半区），加载地址仍是物理 0x100000（即 1MB）
gdt.c / .h      GDT（ring 0 / ring 3 段 + TSS）和 TSS 的 esp0（v0.16）
sched.c / .h    调度器：任务表、轮转、抢占、僵尸回收、FXSAVE/FXRSTOR（v0.17）
sched_asm.asm   switch_to：保存 / 恢复内核栈和 callee-saved 寄存器（v0.17）
usermode.c / .h 用户页、int 0x80 系统调用、ring 3 故障恢复、run 的实现（v0.16）
usermode_asm.asm user_enter / gdt_flush（v0.16；v0.17 起任务结束走 sched_exit）
userprog.asm / userfault.asm / userloop.asm 内嵌的 ring 3 演示程序 / 故意缺页 / 后台 tick 循环（v0.16~v0.17）
gfx.c / .h      VBE 帧缓冲：LFB 映射、8×16 文字、矩形 / 圆 / 渐变（v0.18）
check_ppm.py    测试用：读 QEMU screendump 的 PPM，逐像素校验（v0.18）
mouse.c / .h    PS/2 鼠标：aux 通道、IRQ12、3 字节包（v0.19）
pci.c / .h      PCI 配置空间、按 class 找控制器（v0.20）
usb.c / .h      UHCI + USB 枚举 + HID 平板绝对坐标（v0.20）
wm.c / .h       桌面与窗口：指针、拖动、焦点（v0.19）
userbin.asm     用 incbin 把两个用户程序嵌进内核（v0.16）
Makefile        拼出可启动镜像 build/os-image.bin
test-boot.sh    自动化引导测试（macOS / Linux 双平台可用）
```

引导层两个汇编文件（`boot.asm` + `entry.asm`）去掉注释后合计 277 行，只做"把 CPU 从 16 位
带到 32 位"这件事。当前版本（v0.16）所有 `.asm` 加起来约 590 行：除了引导层，还有中断入口桩
`isr.asm`（104 行）、SSE 测试 `simd_test.asm`（28 行），以及 v0.16 起内嵌的两个 ring 3 用户程序
`userprog.asm` / `userfault.asm` 和用户态切换垫片 `usermode_asm.asm`。
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
| `ld -m elf_i386` | Zig 内置的 **LLD**（`zig ld.lld`） |
| `objcopy -O binary` | `elf_to_bin.py`（几十行 Python，`zig objcopy` 曾经卡住） |
| `qemu-system-i386` | UTM 内置的 QEMU（封装成 `toolchain/qemu-i386`） |

**没有这套工具链也能编译**（v0.6.1 起）：Makefile 找不到它时会自动改用系统里的
`nasm` / `clang` / `ld.lld` / `qemu-system-i386`。Linux 上装好这几个包直接 `make` 即可：

```bash
sudo apt-get install nasm clang lld qemu-system-x86 python3
```

## 编译 + 运行

```bash
cd zeroOS/versions/v0.23-agent
make                 # 生成 build/os-image.bin（默认从命令行启动）
make AUTO_DESKTOP=1  # 生成开机直接进桌面的镜像
make run    # 用 QEMU 启动它
```

如果一切正常，QEMU 窗口里只有一句欢迎语和一个命令行提示符——
直接开机，不做开机动画/logo 这些"杂七杂八"的东西，启动日志也只走串口：

```
zeroOS v0.23
Type 'help' for a list of commands.

zeroOS> _
```

**在 QEMU 的窗口里**（不是终端里）打命令：

| 命令 | 作用 |
|---|---|
| `help` | 列出所有命令 |
| `clear` | 清屏 |
| `echo ...` | 原样打印后面的文字 |
| `uptime` | 开机多久了（TSC 计时），并列出时钟中断次数 |
| `mem` | BIOS 内存地图、空闲页数、分页和堆的状态 |
| `memtest` | 自检：物理页、分页、堆、按需分页、采样、低地址不映射共 26 项 |
| `cpu` | 处理器型号和特性，浮点是否开启 |
| `mathtest` | 浮点自检：数学函数精度、特殊值、中断不破坏 XMM 寄存器，共 12 项 |
| `bench` | 实测矩阵运算速度，估算 1B / 3B 模型每个词要多久 |
| `disks` | 列出硬盘（目前只支持 ATA/IDE） |
| `diskread <盘> <扇区>` | 显示一个扇区开头的内容 |
| `diskcheck <盘> <起始> <数量>` | 校验测试硬盘上的图案 |
| `diskbench <盘> <MB>` | 测读盘速度，估算读 1 / 1.5 / 3 GB 要多久 |
| `metrics` | 感知层：各计数器的累计值、最近 1 秒的速率、CPU 忙碌比例 |
| `events` | 最近的重要事件（溢出、非法释放、按需缺页……） |
| `telstream on\|off` | 每 100 毫秒往串口输出一行采样（采集数据用） |
| `tools` | 列出所有工具和权限等级 |
| `tool <名字> [参数]` | 通过工具层调用一个工具：按等级直接执行 / 弹确认框 / 拒绝，并写审计 |
| `devmode on\|off` | 开发者模式（解锁系统级工具；打开要过确认框） |
| `audit` | 工具调用的审计日志 |
| `sleep <毫秒>` | 等一会儿（期间敲的键会排队） |
| `run` | 在后台起一个 ring 3 程序（`run loop` 跑 2 秒 tick 循环，`run fault` 故意缺页） |
| `gfx` | 画一个帧缓冲演示（渐变 / 矩形 / 圆） |
| `desktop` | 进入桌面：鼠标 + 两个窗口，ESC 回命令行 |
| `net` | 显示 MAC / IP / 网关 / 链路 |
| `ping <ip>` | ICMP echo，打印往返时间 |
| `dns <name>` | 解析 A 记录并打印 IP |
| `wminfo` | 打印窗口坐标和焦点（测试用） |
| `jobs` | 列出后台任务和状态 |
| `kill <id>` | 杀掉一个后台任务 |
| `shutdown` | 关机：写 ACPI PM1a_CNT 让宿主机断电；真机关不掉时退化成停机 |
| `version` | 内核版本（v0.23） |

屏幕上的所有内容也会同步写到串口，`make run` 的终端里能看到一模一样的一份。
终端里还会先打出**十七行**启动日志：

```
zeroOS kernel v0.23: entered protected mode, kmain() started
zeroOS kernel v0.23: IDT installed, CPU exceptions now caught
zeroOS kernel v0.23: PIT at 100 Hz, IRQ0 enabled
zeroOS kernel v0.23: PS/2 keyboard on IRQ1 enabled
zeroOS kernel v0.23: FPU + SSE2 enabled
zeroOS kernel v0.23: PS/2 mouse on IRQ12 enabled
zeroOS kernel v0.23: clock source: TSC
zeroOS kernel v0.23: physical memory manager ready
zeroOS kernel v0.23: paging: higher-half kernel at 0xC0100000, low 3GB unmapped
zeroOS kernel v0.23: user mode: ring 3 segments + TSS ready
zeroOS kernel v0.23: graphics: 1024x768x32 via VBE
zeroOS kernel v0.23: kernel heap ready at 0xF0000000
zeroOS kernel v0.23: telemetry sampling every 100 ms
zeroOS kernel v0.23: ATA disks found: 1
zeroOS kernel v0.23: shell ready on screen and COM1
zeroOS kernel v0.23: agent bridge listening on COM1 (protocol v1)
zeroOS kernel v0.23: tool layer ready, developer mode off
```

判断方法：

- 一行都没出现 → 没进到 `kmain`，问题在 boot 这边（GDT / 保护模式切换）
- 只出现第一行、没有第二行 → 卡在 `idt_install()` 附近
- 停在 `physical memory manager ready` 或 `paging: higher-half` 之前 → 内存初始化的问题（E820 地图、页表），用 `make shot` 看屏幕上的异常信息
- 启动日志（十五行）都出现了、打字没反应 → 确认焦点在 QEMU 窗口里；仍不行就是键盘中断（IRQ1）的问题
- 启动日志都有、终端里有命令输出、但屏幕上没有 → 问题在 `vga.c`（显存写入）

**挂一块测试硬盘**：`make run` 只挂启动盘。想试硬盘命令，先生成测试盘，再手动启动 QEMU：

```bash
python3 gen_testdisk.py build/testdisk.img 64
```

然后在 QEMU 命令里加上 `-drive format=raw,file=build/testdisk.img,index=1`，进系统后 `disks`、`diskcheck ata1 0 131072`。
`./test-boot.sh` 会自动生成并挂上测试盘。

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
zeroOS kernel: CPU exception -> Division By Zero at EIP 0x0000131A
system halted.
```

（v0.8 起屏幕上也会用红字显示同样的内容）

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
- **v0.6** — 新增 PIT 定时器（`pit.c`/`pit.h`）：通道 0 设成 100 Hz，解除 IRQ0 屏蔽并 `sti`，
  每 100 个 tick 往串口打一行 `timer tick`；`elf_to_bin.py` 替代会卡住的 `zig objcopy`；
  `test-boot.sh` 改成必须等到 tick 出现才算通过
- **v0.6.1** — 底层修正，运行行为不变：构建时检查内存上限（含 `.bss`）；`entry.asm` 清零 `.bss`；
  中断入口 `cld`；处理 IRQ7/15 伪中断；`KERNEL_SECTORS` 只维护一处；构建可移植 + GitHub Actions 自动测试
- **v0.7** — 新增 PS/2 键盘驱动（IRQ1）：Shift / Caps Lock / 退格 / 回车，字符回显到串口，回车打印整行；
  去掉每秒一行的 `timer tick`；测试改用 QEMU `sendkey` 自动按键
- **v0.8** — 屏幕文字输出（80×25、滚屏、光标）+ 内核命令行（`help` / `clear` / `echo` / `uptime` / `mem` / `version`）；
  屏幕和串口同步输出；异常信息显示在屏幕上；测试直接读显存核对屏幕内容
- **v0.9** — 内存管理：引导扇区改用 LBA 读盘、要 E820 内存地图、把内核搬到 1MB；物理页分配、
  分页（第 0 页不映射，空指针直接报错）、内核堆 `kmalloc` / `kfree`；`memtest` 自检；禁用 SSE 代码生成
- **v0.10** — 开启 FPU / SSE2，数学函数，`cpu` / `mathtest` / `bench`；核心代码与浮点代码分开编译；
  核心代码按 486 生成指令（修正：v0.6 ~ v0.9 实际要求 Pentium Pro 以上）
- **v0.10.1** — 开机时间改用 TSC（满负荷时数中断会丢，实测最多慢 19%）；堆释放时归还末尾多余的页
- **v0.11** — 块设备接口 + ATA PIO 硬盘驱动（支持 LBA48），只读；`disks` / `diskread` / `diskcheck` / `diskbench`；
  QEMU 里读盘约 7–8 MB/s。AHCI / NVMe 以后加
- **v0.12** — 感知层：中断、键盘、堆、物理页、缺页、CPU 空闲时间等 18 个计数器，时钟中断里每 100 毫秒采样一次，
  事件日志；按需分页区（缺页现场补页、不再停机）；`metrics` / `events` / `telstream`
- **v0.13** — 工具层：11 个工具、4 级权限、内核绘制的确认框（只认物理键盘、丢弃提前敲好的键、20 秒超时）、
  开发者模式、审计日志；`heap_check` / `pmm_check` 诊断；`inject_fault` 故障注入。Agent 以后只能通过这一层做事
- **v0.13.1** — 复审修正：`spin` 参数越界读、故障注入后 `memtest` 误报、确认框文字截断等 4 处（另 1 处复测后撤回）
- **v0.13.2** — 全量复审 + 8 组压力测试，修 14 处：堆扩张失败吞页、页表不回收、Ctrl 组合键漏字符、遥测流粘连、
  引导扇区验证 A20、栈哨兵与水位（`stack_check`）等
- **v0.14** — 桥接协议内核侧：串口接收（IRQ4，115200）、最小 JSON、hello / call / confirm / result / event / verdict；
  串口调用走同一个工具层，确认框只认物理键盘；`bridge_test.py` 扮演宿主机走 35 步协议
- **v0.15** — 高半内核：内核搬到 `0xC0100000`，物理内存映射到 `0xC0000000` 起（窗口 768MB），0 ~ 3GB 留给用户态；
  堆挪到 `0xF0000000`、按需分页区挪到 `0xF8000000`；`memtest` 26 项（新增低地址不再映射、显存经窗口可达）
- **v0.15.1** — 新增 `shutdown`：写 ACPI PM1a_CNT 让宿主机断电（QEMU pc / q35 实测都能关），ACPI 不可用时退化成停机；`io.h` 新增 `outw`
- **v0.16** — 用户态：GDT 加 ring 3 段 + TSS，`int 0x80` 系统调用（exit / write / getkey / uptime / tool / sleep）；用户页带 USER 位、指针校验；
  ring 3 里缺页 / 非法指令只杀程序、回 shell；`run` / `run fault` 跑内嵌的 ring 3 程序
- **v0.17** — 抢占式调度：每个任务独立 8KB 内核栈 + 16MB 虚拟窗口，30ms 时间片轮转；
  shell 等键盘时让出 CPU、有按键立刻回来；FXSAVE/FXRSTOR 浮点上下文；`run` 变后台任务，新增 `jobs` / `kill`
- **v0.18** — 图形模式：boot 用 VBE 切 1024×768×32，内核映射 LFB，文字控制台画到帧缓冲；
  `gfx` 命令演示渐变 / 矩形 / 圆；测试用 `screendump` 抓帧缓冲逐像素校验
- **v0.19** — 桌面：PS/2 鼠标（IRQ12）+ 两个窗口，拖动 / 焦点 / ESC 回命令行；
  开机默认画深色渐变背景，不再黑底文字；`desktop` / `wminfo` 命令
- **v0.20** — USB 平板：PCI 找 UHCI、端口复位与枚举、HID 中断 IN 读绝对坐标；
  桌面优先用绝对指针（UTM 的 usb-tablet），PS/2 作回退
- **v0.20.1** — 双缓冲合成：桌面画后台缓冲、按脏区拷前台，拖动不再闪；
  窗口阴影 / 标题栏 / 任务栏 / Exit 按钮；ESC、q、Exit、60 秒空闲四路退出，不会被锁在桌面
- **v0.20.2** — `AUTO_DESKTOP=1`：开机直接进桌面，退出即 shell；默认构建仍从命令行开始
- **v0.21** — 第一次联网：legacy virtio-net + ARP / IPv4 / ICMP / UDP / DNS；
  `net`、`ping <ip>`、`dns <name>`；还没有 TCP/TLS，不能浏览网页
- **v0.22.1** — 拖动顺滑：预渲染无窗场景 + 窗口精灵，拖动只做内存拷贝；
  标题文字透明背景，窗口周围不再有背景残影
- **v0.23（当前）** — 宿主侧 Agent 循环接进协议 v1：`agent/` 可握手、拿工具清单、
  调用工具（scripted / OpenAI 兼容后端）；桌面模式下桥接也由桌面循环轮询

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
├── v0.6-pit/                 ← v0.6 快照
├── v0.6.1/                   ← v0.6.1 快照
├── v0.7-keyboard/            ← v0.7 快照
├── v0.8-shell/               ← v0.8 快照
├── v0.9-memory/              ← v0.9 快照
├── v0.10-float/              ← v0.10 快照
├── v0.10.1/                  ← v0.10.1 快照
├── v0.11-disk/               ← v0.11 快照
├── v0.12-telemetry/          ← v0.12 快照
├── v0.13-tools/              ← v0.13 快照
├── v0.13.1/                  ← v0.13.1 快照
├── v0.13.2/                  ← v0.13.2 快照
├── v0.14-bridge/
├── v0.15-highhalf/
├── v0.15.1/
├── v0.16-usermode/
├── v0.17-sched/
├── v0.18-gfx/
├── v0.19-mouse/
├── v0.20-usb/
├── v0.20.1/
├── v0.20.2/
├── v0.21-net/
├── v0.22.1/
├── v0.23-agent/               ← 当前版本（内核 + agent/）
└── experiments/                  ← 不属于版本线的独立实验（图形模式画圆环）
```

每个版本目录里都有一个 **`TEST-RESULTS.md`**，记录：这一版是什么、测了什么、
结果如何、查出了什么缺陷、证据文件在哪。

`serial-output.txt` 是用该版本**自己的源码**重新构建并启动后抓取的原始串口输出。

复现方式随版本而变：早期版本（内核不超过 40 扇区、只有 `kernel` / `idt` / `pic` 等少数模块）
可以用 `./versions/build-and-test.sh <版本目录名>` 重新构建；v0.13 之后模块数到 30+、内核预留
128 KB，该脚本跑不了，请直接用版本目录里的 `./test-boot.sh`（详见 [`versions/README.md`](versions/README.md)）。

## 兼容性：为什么镜像要补零到 512 KB

`make` 构建出来的 `build/os-image.bin` 实际内容只有 **131,584 字节**（512 字节引导扇区 + 补齐到
128 KB 的内核；v0.16 的内核真实代码约 71 KB，本机实测 71,340 字节），但**会被补零到 512 KB**。

原因是磁盘控制器的世代差异：

| 控制器 | QEMU 机型 | 对 131 KB 镜像 |
|---|---|---|
| **IDE / PATA**（1986） | `-M pc`（i440FX + PIIX，1996） | ✅ 接受 |
| **AHCI / SATA**（现代） | `-M q35`（Q35 + ICH9，2009） | ❌ **拒绝启动** |

AHCI 路径下 SeaBIOS 要按容量推算磁盘几何，盘太小会推算失败，于是它认为
"这不是一块可启动的盘"，**连引导扇区都不载入** —— 表现是静默黑屏，一个报错都没有。
实测阈值：≤256 KB 失败，≥512 KB 成功。

**现代真机和现代虚拟机用的都是 AHCI**，所以这不是冷门机型的怪癖，而是真实的可移植性问题。
补进去的全是零，不影响引导层真正读取的那 256 个扇区（`KERNEL_SECTORS`，见 Makefile）。

实测（补零后）：

```
pc    成功 15 / 15
q35   成功 15 / 15
```

至于读盘方式：`boot.asm` 从 v0.9 起已经改成 **LBA 扩展读（`AH=42h`）**，不再依赖柱面 / 磁头 / 扇区几何。
但注意：**这修不了上面这个启动问题** —— AHCI 拒绝启动时引导扇区压根没被执行，里面写什么都无所谓；
补零解决的是"BIOS 肯不肯把这块盘当成可启动的盘"，LBA 解决的是"容量与几何寻址"。两件事互不替代。

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

## 下一步做什么

路线按"一版一个目录 + 测试通过再发布"推进，细节写在设计文档里：

| 顺序 | 内容 | 文档 |
|---|---|---|
| 下一步 | **v0.17 任务切换**：调度器、抢占、浮点上下文（`FXSAVE`），命令行和用户程序同时跑 | [`docs/agent-design.md`](docs/agent-design.md) |
| 之后 | **v0.18 存储 / 文件系统**：审计日志落盘、文件类工具 | 同上 |
| 之后 | **Agent 搬进 zeroOS**：协议从串口换成系统调用（第一阶段 B 的宿主机程序不动，只换传输层） | [`docs/agent-bridge-protocol.md`](docs/agent-bridge-protocol.md) |
| 之后 | **64 位（长模式）**：更大地址空间，支持 3B 以上的模型 | 同上 |

短期还可以做的小事：

- 引导层想在"任意 BIOS"上更稳，可以补 E820 / `INT 13h` 返回码的兜底（读盘已经是 LBA 扩展读）
- 把 CI 里的 QEMU / clang 版本固定下来，避免上游升级导致测试结果漂移
- 用户态继续补齐：`fork` / `wait`、按程序单独配额、把工具调用从 ring 3 走完整的审计链
