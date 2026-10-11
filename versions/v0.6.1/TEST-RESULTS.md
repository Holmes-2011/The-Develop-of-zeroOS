# v0.6.1 测试记录

## 本版内容

v0.6 的底层修正版，**运行行为不变**（仍是 100 Hz 时钟、每秒一行 tick），只修 v0.6 审查时发现的隐患：

| # | 问题 | 修法 | 文件 |
|---|---|---|---|
| ① | 内核（含 `.bss`）长到 `0x7C00` 会盖掉引导扇区里的 GDT，每次中断重载段寄存器都要读它；旧检查只看文件大小，`.bss` 不占文件空间，查不出来 | `elf_to_bin.py --mem-limit 0x7C00` 按内存末端检查；`link.ld` 导出 `__kernel_end` | `elf_to_bin.py` / `link.ld` / `Makefile` |
| ② | `.bss` 没人清零，只是碰巧落在补零区里 | `entry.asm` 在调 `kmain` 前用 `rep stosb` 清零 `__bss_start ~ __bss_end` | `entry.asm` / `link.ld` |
| ③ | 中断入口没 `cld`：被打断的代码若设了 DF，C 里的字符串操作会往反方向写 | `isr_common_stub` 里 `pusha` 之后 `cld` | `isr.asm` |
| ④ | IRQ7 / IRQ15 伪中断会被照常 EOI，可能把别的中断"确认"掉 | `pic_is_spurious()` 读 ISR 判断；IRQ15 伪中断只给主片 EOI | `pic.c` / `pic.h` / `kernel.c` |
| ⑤ | `KERNEL_SECTORS` 在 `boot.asm` 和 `Makefile` 里各写一份 | Makefile 用 `nasm -DKERNEL_SECTORS=...` 传进去 | `boot.asm` / `Makefile` |
| ⑥ | 工具链路径写死在作者本机，别人无法编译 | 找不到 `c-dev-apps` 就用系统的 `nasm` / `clang` / `ld.lld` / `qemu-system-i386`；`stat -f%z` 换成 `wc -c`（Linux 也能用）；链接改为直接调 LLD | `Makefile` |
| ⑦ | 过时的注释 / 串口里的版本号 | 更新 | 多处 |

另外新增 GitHub Actions：每次推送都在 Ubuntu 上从零编译并 QEMU 启动测试（`.github/workflows/build-test.yml`）。

## 测试结果

| 测试项 | 结果 |
|---|---|
| `make` + `./test-boot.sh` | ✅ 5/5 行全部出现 |
| `-M pc` 重复启动 | ✅ **10/10**（每次只启动一次 + 至少 2 次 tick） |
| `-M q35` 重复启动 | ✅ **10/10** |
| tick 频率 | ✅ 10.5 秒 → 10 行 tick，只启动一次 |
| 异常路径（取消注释 `int $0x0`） | ✅ 打印 `Division By Zero` 后安全停机，之后不再有 tick |

### 对照实验 1：`.bss` 清零（②）

把镜像里 `.bss` 对应的区域故意填满 `0xAA`，启动 2 秒后用 QEMU monitor 读回整段 `.bss`（2060 字节）：

| 版本 | 读回后仍是 `0xAA` 的字节数 |
|---|---|
| v0.6（修复前） | **3**（C 代码没有显式写过的填充字节） |
| v0.6.1（修复后） | **0** |

v0.6 里 IDT、`ticks` 恰好都被代码显式初始化过，所以这 3 个字节暂时没造成问题；
以后新增任何"依赖默认为 0"的全局变量，在旧版上都会拿到垃圾值。

### 对照实验 2：内存上限检查（①）

在 `kernel.c` 末尾临时加一个 24 KB 的全局数组（只占 `.bss`，不占镜像文件）：

```
错误：内核在内存里一直延伸到 0x87D4（含 .bss），超过了 0x7C00
make: *** [build/kernel.bin] Error 1
```

旧版只检查文件大小，同样的改动会**通过构建**，启动后第一次中断就会因 GDT 被覆盖而出错。
当前版本实际占用 `0x1000 ~ 0x27C8`。

### 无法在本机实测的部分

| 项目 | 说明 |
|---|---|
| ④ 伪中断 | QEMU 不会产生伪中断，只能确认正常 IRQ0 路径不受影响（tick 频率不变）；按 8259A 手册与 OSDev 通行做法实现 |
| ⑥ Linux 构建 | 本机是 macOS，由 GitHub Actions 在 Ubuntu 上验证 |

测试环境同 [`../README.md`](../README.md)：macOS + UTM 内置 QEMU，32 MB 内存。

## 串口输出

见 [`serial-output.txt`](serial-output.txt)。
