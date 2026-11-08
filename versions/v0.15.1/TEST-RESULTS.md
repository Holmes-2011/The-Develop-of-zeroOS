# v0.15.1 测试记录

## 本版内容

**新增 `shutdown` 命令**：往 ACPI 的 PM1a 控制寄存器（`0x604`）写 SLP_EN，让宿主机断电。
这是 v0.15 之后的小版本，不改地址空间、不改协议，只加一个命令和它的测试。

| 改动 | 文件 |
|---|---|
| `shutdown`：q35 用 16 位写 `0x3400`（S5：SLP_TYP=5 \| SLP_EN），pc 的 PM1a_CNT 只认字节写，高低字节各写一次（= `0x2000`）；关不掉时退化成 `cli; hlt` 并打印说明 | `shell.c` |
| `outw`：16 位端口写 | `io.h` |
| 版本串 `v0.15` → `v0.15.1`：启动日志、`version` 命令、桥接 `welcome` 里的 kernel 字段 | `kernel.c`、`shell.c`、`bridge_test.py` |
| 测试第三阶段：单独起一台 QEMU 敲 `shutdown`，确认串口打出 `powering off`，并且 QEMU 在脚本发 `quit` 之前自己退出（ACPI 断电生效） | `test-boot.sh` |

## 测试方法

`make` 后跑 `./test-boot.sh`（键盘阶段 → 桥接协议 35 步 → 关机阶段）。矩阵一次只跑一台 QEMU，
避免 100 毫秒采样和键盘时序被 CPU 争用干扰（v0.15 记录过这个坑）。

## 测试结果

| 测试项 | 结果 |
|---|---|
| `-M pc` 32MB 全套 | ✅ **143/143**（140 项 + 3 项 shutdown） |
| `-M q35` 32MB 全套 | ✅ **138/138** |
| 486（无 SSE2） | ✅ **138/138**，`memtest` 26/26 |
| 2MB 内存 | ✅ **139/139** |
| 1GB 内存（截到 768MB 窗口） | ✅ **143/143** |
| q35 + 4GB 内存 | ✅ **138/138** |
| 模拟用户 10 轮（`soak.sh`） | ✅ 0 异常、0 失败；`memtest` 10/10；空闲页回到初值 |

### 关机阶段

```
---- 关机（shutdown，-M pc）----
[通过] help 列出了 shutdown
[通过] shutdown：串口打出 powering off
[通过] shutdown：QEMU 在 quit 之前自己退出（ACPI 断电生效）
```

`shutdown` 的串口输出（断电是异步的，打印可能在中途被截断）：

```
powering off...
zeroOS: shutdown requested, writing ACPI PM1a_CNT
```

判定标准是 **QEMU 进程在脚本发 `quit` 之前自己退出**，不是日志里有没有某一行——
退化成停机时也会打印同样的前两行。

## 已知限制

| 项目 | 说明 |
|---|---|
| 真机 ACPI 没验证 | 结论来自 QEMU 的 `pc` / `q35`；真机的 FADT 里 S5 的 SLP_TYP 可能不是 5，关不掉时会退化成停机并打印说明 |
| QEMU 的两种实现 | `q35` 认 16 位写 `0x3400`；`pc` 的 PM1a_CNT 只认字节写（`0x604` / `0x605` 各写一次）。代码按顺序各试一次，关掉就不再往下走 |
| 关机前不清理 | 还没有文件系统 / 任务，没有需要保存的状态；将来有了要先把脏页 / 日志刷盘 |

## 串口输出

见 [`serial-output.txt`](serial-output.txt)（键盘阶段）和 [`bridge-test.log`](bridge-test.log)（协议阶段）。
