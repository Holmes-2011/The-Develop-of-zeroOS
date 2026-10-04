# v0.2-serial — 测试记录

## 这一版是什么

上游标注的 **v0.2**。在 v0.1（清屏 + 停机）的基础上新增 **COM1 串口调试输出**。

| 文件 | 大小 | 内容 |
|---|---|---|
| `kernel.c` | 2397 字节 | 串口初始化（COM1 = `0x3F8`，轮询方式）+ 清屏 + 停机 |

串口初始化序列（标准 16550 UART 套路）：

```c
outb(COM1 + 1, 0x00);   /* 关掉串口中断，用轮询 */
outb(COM1 + 3, 0x80);   /* 打开 DLAB，准备设波特率 */
outb(COM1 + 0, 0x03);   /* 波特率除数低字节（38400）*/
outb(COM1 + 1, 0x00);   /* 波特率除数高字节 */
outb(COM1 + 3, 0x03);   /* 8N1，关掉 DLAB */
outb(COM1 + 2, 0xC7);   /* 开 FIFO，清收发缓冲 */
outb(COM1 + 4, 0x0B);   /* 按标准置 IRQ 相关位（实际用轮询）*/
```

## 上游原包

`upstream-source.zip`（4164 字节）—— 上游发的原始压缩包，
含 `kernel.c` / `Makefile`（Linux 版）/ `README.md`。

> 注：这个包我手上只保留了 `kernel.c` 的副本，zip 本身在整理过程中被后续版本的包取代了。

## 测试项与结果

⚠️ **这一版是在测试体系还不完善的时候测的，覆盖面比 v0.3 之后的版本窄。**

| 测试项 | 结果 |
|---|---|
| 编译（clang `--target=i386-unknown-none`） | ✅ 成功 |
| 启动 | ✅ 成功 |
| 串口输出 | ✅ 两行完整 |
| 屏幕是否纯黑 | ✅ 是（`0xB8000` 全是 `0x20 0x00`） |

实际串口输出：

```
zeroOS kernel v0.2: entered protected mode, kmain() started
zeroOS kernel v0.2: screen cleared, entering halt loop
```

**未做的测试**（当时还没建立这套流程）：

- ❌ 多次重复启动（没有成功率数据）
- ❌ 多机型扫描
- ❌ 异常向量逐个触发

## 这一版暴露的缺陷

和 v0.1 相同：引导层进保护模式前缺 `cli`（当时还没发现）。

另外，这一版的 `Makefile` 是**为 Kali Linux 写的**：

```makefile
CC = gcc
CFLAGS = -m32 -ffreestanding ...
LD = ld
    $(LD) -m elf_i386 -T link.ld ...
run:
    qemu-system-i386 -drive format=raw,file=os-image.bin -serial stdio
```

在 macOS 上跑不了 —— `clang -m32` 在 Apple Silicon 上会**静默编成 ARM 目标**
（实测产物是 `Mach-O object arm_v4t`），比报错更难排查。工程里现在用的
Makefile 已换成 macOS 版（clang 交叉目标 + Zig 的 LLD + UTM 内置 QEMU）。

## 本目录内的证据文件

| 文件 | 说明 |
|---|---|
| `kernel.c` | 该版本源码（2397 字节） |
| `serial-output.txt` | 2 行串口输出 |

> `serial-output.txt` 由 `../build-and-test.sh v0.2-serial` 生成 —— 用**该版本自己的源码**重新构建并启动后抓取的原始串口输出，可随时复现。
> 该脚本只编译版本目录里实际存在的模块，不会混入其它版本的代码。
