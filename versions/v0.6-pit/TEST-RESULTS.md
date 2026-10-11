# v0.6-pit 测试记录

## 本版内容

在 v0.5.1 基础上新增 **PIT（8253/8254 可编程定时器）**，让内核第一次真正收到硬件中断：

| 改动 | 文件 |
|---|---|
| PIT 通道 0 设成 100 Hz（方式 3，分频 1193182 / 100） | `pit.c` / `pit.h`（新增） |
| 解除 PIC 的 IRQ0 屏蔽（`pic_unmask_irq`），然后 `sti` 开中断 | `pic.c` / `pic.h` / `kernel.c` |
| IRQ0 每触发一次 `pit_tick()` 加一；主循环每 100 tick 往串口打一行 `zeroOS timer tick` | `kernel.c` |
| 用 `elf_to_bin.py` 替代会卡住的 `zig objcopy` | `elf_to_bin.py`（新增）/ `Makefile` |
| 测试必须等到至少 3 行 `timer tick` 才算通过 | `test-boot.sh` |

## 为什么要改测试脚本

旧版 `test-boot.sh` 一看到 `entering halt loop` 就杀掉 QEMU（约 0.13 秒），
这时 PIT 还没来得及跳满 100 次，**一行 tick 都不会出现**——
也就是说，就算时钟中断完全没在工作，旧脚本也会报"全部通过"。
现在改成等到 `MIN_TICKS=3` 行 tick（超时 8 秒），并把 `timer tick` 列为必须出现的行。

## 测试结果

| 测试项 | 结果 |
|---|---|
| `make` 构建 | ✅ 内核 3924 字节，补齐到 40 扇区；镜像补零到 512 KB |
| `./test-boot.sh` | ✅ 5/5 行全部出现，约 3.2 秒拿到 3 次 tick |
| `-M pc` 重复启动 | ✅ **10/10**（每次只启动一次 + 至少 2 次 tick） |
| `-M q35` 重复启动 | ✅ **10/10** |
| tick 频率 | ✅ 运行 10.5 秒 → **10 行** tick（= 100 Hz × 100 tick/行），只启动一次、无重启循环 |

测试环境同 [`../README.md`](../README.md)：macOS + UTM 内置 QEMU，32 MB 内存。

## 串口输出

见 [`serial-output.txt`](serial-output.txt)。

## 后续

- IRQ1 键盘驱动
- 把 tick 暴露给后续的终端命令（例如 uptime）
