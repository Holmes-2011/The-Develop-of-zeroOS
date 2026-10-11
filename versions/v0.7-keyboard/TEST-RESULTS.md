# v0.7-keyboard 测试记录

## 本版内容

在 v0.6.1 基础上新增 **PS/2 键盘驱动（IRQ1）**，内核第一次能接收用户输入：

| 改动 | 文件 |
|---|---|
| 键盘驱动：读 `0x60` 扫描码（第 1 套）→ 翻译成字符（美式布局）→ 放进 128 字节环形缓冲区 | `keyboard.c` / `keyboard.h`（新增） |
| 支持 Shift（左右分开记）、Caps Lock、退格、回车；`0xE0` 扩展键（方向键等）先忽略 | `keyboard.c` |
| 放开 IRQ1，`isr_handler` 把它转给 `keyboard_irq()` | `kernel.c` |
| 主循环：取字符 → 回显到串口 → 拼成一行（最长 79 字符）→ 回车时打印 `zeroOS input: ... (ticks=N)` | `kernel.c` |
| 主循环用 `cli` 检查缓冲区 + `sti; hlt`，避免"检查完、睡下之前恰好来了按键"导致的延迟 | `kernel.c` |
| 去掉每秒一行的 `timer tick`（会打断输入回显）；时钟改由整行末尾的 `ticks=N` 验证 | `kernel.c` |
| 测试改为用 QEMU monitor 的 `sendkey` 自动模拟按键 | `test-boot.sh` |

## 测试方法

`test-boot.sh` 等内核打出 `waiting for keyboard input` 后，通过 QEMU monitor 依次按下：

```
shift-h e l l o comma spc z e r x backspace o shift-o shift-s shift-1 spc caps_lock o k caps_lock ret
```

期望内核打出 `zeroOS input: Hello, zeroOS! OK (ticks=N)`，且 N > 0。
这一行覆盖了：Shift + 字母、Shift + 数字（`!`）、符号（`,`）、空格、退格删字、Caps Lock 开关。

## 测试结果

| 测试项 | 结果 |
|---|---|
| `make` | ✅ 无警告；内存占用 `0x1000 ~ 0x2CE0`，上限 `0x7C00` |
| `./test-boot.sh`（`-M pc`）重复 | ✅ **10/10** |
| `MACHINE=q35 ./test-boot.sh` 重复 | ✅ **10/10** |
| 压力测试：连敲 100 个 `a` + 回车，再敲 `ok` + 回车 | ✅ 第一行截断为 79 个字符，第二行正常输出 `ok`，无溢出、无卡死 |

### 对照实验：测试能不能抓到故障

| 故意弄坏 | 测试结果 |
|---|---|
| 不放开 IRQ1（删掉 `pic_unmask_irq(1)`） | ❌ 报缺失：没有输入行（正确地失败） |
| Shift 失效（总是查 `keymap_normal`） | ❌ 报缺失：内核打出的是 `hello, zeroos1 OK`（正确地失败） |

### 排查记录：压力测试第一次"丢键"

第一次压力测试只收到 79 个 `a`，后面的回车和 `ok` 都没出现。
用 QEMU monitor 检查：CPU 停在 `hlt`、中断开着、PIC 没有卡住的中断；
缓冲区计数 0.5 秒内从 73 涨到 102 —— **按键还在陆续进来，是 QEMU 在排队慢慢送**，
测试脚本只等了 1 秒就退出了。改成"等内核打出整行再发下一批"后通过。不是内核问题。

### 无法在本机实测的部分

| 项目 | 说明 |
|---|---|
| 真机 PS/2 / USB 键盘 | QEMU 模拟的是 i8042 + PS/2 键盘。真机上 USB 键盘依赖 BIOS 的"USB Legacy"模拟成 PS/2，多数机器默认开启；原生 USB 驱动是很久以后的事 |
| 非美式键盘布局 | 只做了美式布局 |

## 串口输出

见 [`serial-output.txt`](serial-output.txt)（里面的 `^H` 是退格回显 `\b \b`）。
