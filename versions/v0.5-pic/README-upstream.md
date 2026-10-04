# zeroOS 引导层最小实现

**当前版本：v0.5**（见本文档末尾"版本记录"）

## 为什么不能 100% 用 C 写

x86 开机时 CPU 处于 **16 位实模式**，BIOS 只做一件事：把磁盘第一个 512 字节的扇区
（"引导扇区"）加载到内存地址 `0x7c00`，然后直接跳过去执行——此时没有栈约定、
没有 C 运行时初始化、甚至没人替你设好段寄存器。这个阶段 C 编译器帮不上忙，
所以行业里所有引导层（包括 GRUB、Linux 的 boot.S、Windows 的 bootmgr）都是
先用一小段汇编做"开机自检 + 切换到保护模式"，然后再跳进真正的高级语言代码。

这份代码就是这个思路的最小实现：

```
boot.asm          16位汇编：BIOS入口 -> 用磁盘中断加载内核 -> 开A20 -> 切换到32位保护模式
kernel_entry.asm   32位汇编，只有3行：把控制权交给 C 函数 kmain()
kernel.c           真正的逻辑，纯 C，直接操作显存打印文字
link.ld            告诉链接器内核要被放在内存的哪个地址
Makefile           把以上几个文件拼成一张可启动的磁盘镜像 os-image.bin
```

汇编部分加起来不到 100 行，而且只做"把 CPU 从 16 位带到 32 位"这一件事，
以后所有的功能（内存管理、进程、文件系统……）都可以在 `kernel.c` 里用纯 C 写。

## 在哪里编译

你 Mac 本体空间只剩 40 多 G，而且 macOS 自带的是 clang 不是真正的 gcc，
编译裸机代码要装一整套交叉工具链，比较折腾。建议直接在你已经装好的
**Kali Linux UTM 虚拟机**里编译，把这个项目目录放在你的 2T 机械硬盘上、
再挂载进虚拟机，这样也不占 Mac 本体的存储。

在 Kali 里装好工具链（只需一次）：

```bash
sudo apt update
sudo apt install nasm gcc-multilib qemu-system-x86
```

## 编译 + 运行

```bash
cd zeroos-boot
make        # 生成 os-image.bin
make run    # 用 QEMU 启动它
```

如果一切正常，QEMU 窗口会直接变成一片纯黑，没有任何文字、没有图案——
这是故意的：直接开机，不做开机动画/logo 这些"杂七杂八"的东西。

想确认内核是不是真的跑起来了、而不是卡死在某一步，看终端里的串口输出就行——
`make run` 已经加了 `-serial stdio`，正常情况下终端会打印：

```
zeroOS kernel v0.5: entered protected mode, kmain() started
zeroOS kernel v0.5: IDT installed, CPU exceptions now caught
zeroOS kernel v0.5: PIC remapped to 32-47, all IRQs masked
zeroOS kernel v0.5: screen cleared, entering halt loop
```

想验证 IDT 真的生效了，可以在 `kernel.c` 的 `kmain()` 里取消那行 `int $0x0` 的注释、重新编译，
终端会多打印一行 `zeroOS kernel: CPU exception -> Division By Zero`，然后安全停机，
而不是像以前那样 QEMU 窗口直接静默重启（没有 IDT 的情况下触发异常会变成三重故障）。

这两行一个都没出现 → 没进到 kmain，问题在 boot 这边（GDT/保护模式切换）；
只出现第一行、没有第二行 → 卡在 `clear_screen()` 附近；
两行都出现、但屏幕上有花屏/乱码 → 问题反而不在逻辑上，多半是显存地址算错了。

## 版本记录

- **v0.1** — 直接开机 → 清屏 → 停机，屏幕保持纯黑，无任何画面或文字
- **v0.2** — 新增串口（COM1）调试输出，不影响屏幕观感，仅在终端可见，用于验证内核是否正常运行到哪一步
- **v0.3** — 新增 IDT 异常处理（`idt.c`/`idt.h`/`isr.asm`），捕获 0~31 号 CPU 异常并打印到串口后安全停机，
  不再因为未处理的异常导致虚拟机静默三重故障重启
- **v0.4** — 修了两处正确性问题：① `Makefile` 现在会把 `kernel.bin` 补齐/校验到和 `boot.asm` 里
  `KERNEL_SECTORS` 一致的大小，避免磁盘镜像比引导层要读的范围小，造成越界读；② `boot.asm` 去掉了
  一个多余的 `sti`，整个实模式阶段保持中断关闭直到保护模式内核装好 IDT 为止，消除了一个窗口期的
  三重故障隐患（BIOS 软中断不受 IF 标志位影响，关中断不影响 `int 0x10`/`0x13`/`0x15` 正常使用）
- **v0.5** — 新增 PIC 重映射（`pic.c`/`pic.h`）+ IRQ0~15 入口（`isr.asm`），把硬件中断号从默认的
  0~15 挪到 32~47，避开和 CPU 异常号（0~31）撞车（比如 IRQ0 定时器默认会撞上 8 号"双重故障"异常）。
  现在所有硬件中断仍然全部屏蔽、CPU 中断总开关也没打开，运行行为和 v0.4 完全一样——这一步只是把
  地基搭好，为以后写定时器/键盘驱动做准备，新增 `io.h`（端口读写公共函数）

## 自动化测试（推荐每次改完代码都跑一次）

```bash
make
./test-boot.sh
```

无图形界面跑一次 QEMU，自动抓串口日志、比对是否出现预期的几行关键信息，
通过/失败直接给结论，不用自己盯着窗口判断。串口日志停在哪一行，问题大概率
就出在代码里对应那一步之后——定位问题比"重新审查一遍代码"快得多。

## 下一步可以做什么

- 把 `KERNEL_SECTORS` 和实际内核大小对应起来（现在留了 10KB 余量，够写一阵子）
- 在 `kernel.c` 里加个简单的 `printf` 风格封装，减少手写 offset 计算
- 研究 GDT/分页，为后面上 C 写内存管理做准备
- 如果之后想让内核代码更大、更规范，可以考虑切换成 Multiboot + GRUB 的方式，
  这样引导扇区可以直接交给 GRUB，你的汇编代码能进一步减少
