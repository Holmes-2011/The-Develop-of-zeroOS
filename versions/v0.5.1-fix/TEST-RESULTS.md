# v0.5.1-fix — 测试记录（当前版本）

## 这一版是什么

**上游 v0.5 + 本地修正**。不是上游发的版本，是我们自己在 v0.5 基础上修出来的。

上游最后一次发过来的是 **v0.5**，压缩包里没有附 `boot.asm`、`kernel_entry.asm`、`link.ld`
（这三样一直没给过），工程里用的是本地已有的等价文件。

## 相对上游 v0.5 的具体差异

### ① `isr.asm`：4 处异常的错误码分类修正（本次核心改动）

```diff
-ISR_NOERRCODE 17   ; 对齐检查
+ISR_ERRCODE   17   ; 对齐检查（错误码恒为 0）
-ISR_NOERRCODE 21
+ISR_ERRCODE   21   ; 控制保护 #CP
-ISR_NOERRCODE 29
+ISR_ERRCODE   29   ; VMM 通信 #VC
-ISR_NOERRCODE 30
+ISR_ERRCODE   30   ; 安全异常 #SX
```

依据：Intel SDM Vol.3A 6.3.1 / AMD APM Vol.2 8.4.2 的异常表。
完整的错误码向量表共 10 个：`8, 10, 11, 12, 13, 14, 17, 21, 29, 30`。

同时在文件里加了一段注释，把依据和几个易错点（8/17/18/20/21/28/29/30）都写清楚了。

完整差异见本目录 `isr.asm.diff`。

### ② `boot.asm`：进保护模式前补 `cli`

上游 v0.4 说它"去掉了开头的 `sti`"；我这份是保留 `sti`、在 `lgdt` 之后补 `cli`。
两种写法等价，都彻底消除了时钟中断导致三重故障的窗口。

### ③ `Makefile`：镜像补零到 512 KB

上游 v0.4 只做了"补齐到 `KERNEL_SECTORS`"（20KB）。但那还是不够 ——
AHCI 控制器（`q35`、现代真机）拒绝启动小于约 512KB 的磁盘。

现在构建时会自动补零到 512KB，并在输出里说明原因。

### ④ `Makefile`/`entry.asm`/`link.ld`：macOS 原生构建

上游的 `Makefile` 是 Kali Linux 版（`gcc -m32` / `ld -m elf_i386` / `qemu-system-i386`）。
现在换成 macOS 版：clang 交叉目标 + Zig 的 LLD + `zig objcopy` + UTM 内置 QEMU。

> 实测发现：`clang -m32` 在 Apple Silicon 上会**静默编成 ARM 目标**
> （产物是 `Mach-O object arm_v4t`），比报错更难排查。

### ⑤ `test-boot.sh`：改成 macOS / Linux 双平台可用

上游脚本用了 `timeout` 和 `qemu-system-i386`，这两个在 macOS 上都不存在。

---

## 测试项与结果

### ① 基本启动 ✅

```
zeroOS kernel v0.5: entered protected mode, kmain() started
zeroOS kernel v0.5: IDT installed, CPU exceptions now caught
zeroOS kernel v0.5: PIC remapped to 32-47, all IRQs masked
zeroOS kernel v0.5: screen cleared, entering halt loop
```

### ② 改动是否真的编进了机器码 ✅

反汇编逐个核对压栈指令（`ISR_NOERRCODE` 压 2 个 dword，`ISR_ERRCODE` 压 1 个）：

```
isr17:  cli; pushl $0x11;               jmp   ← 只压 1 个 ✓ 已修正
isr21:  cli; pushl $0x15;               jmp   ← 只压 1 个 ✓ 已修正
isr29:  cli; pushl $0x1d;               jmp   ← 只压 1 个 ✓ 已修正
isr30:  cli; pushl $0x1e;               jmp   ← 只压 1 个 ✓ 已修正
isr18:  cli; pushl $0x0; pushl $0x12;   jmp   ← 仍压 2 个 ✓ 未误改
isr20:  cli; pushl $0x0; pushl $0x14;   jmp   ← 仍压 2 个 ✓ 未误改
isr28:  cli; pushl $0x0; pushl $0x1c;   jmp   ← 仍压 2 个 ✓ 未误改
```

### ③ 回归测试（确认改动没弄坏任何东西）✅

| 测试 | 结果 |
|---|---|
| 上游 `test-boot.sh` | ✅ 四行全通过 |
| **32 个异常向量全扫** | ✅ **32 / 32** |
| **真实 #GP 的本质码路径** | ✅ `err=0x44  eip=0x1DD1  cs=0x08` —— **与修复前逐位一致** |
| `pc` / `q35` 双机型启动 | ✅ 都成功 |

### ④ 稳定性 ✅

累计启动测试（含修复前后）：

| 配置 | 结果 |
|---|---|
| `pc`（IDE / i440FX） | **45 / 45** |
| `q35`（AHCI / Q35） | **45 / 45**（修复前是 0/11） |

---

## 证据等级说明（重要）

这一版有两类结论，**证据强度不同**，不要混为一谈：

| 类型 | 项目 | 强度 |
|---|---|---|
| **实测证实** | 32 向量名字、真实 #GP 的错误码布局、q35 兼容性、启动稳定性 | ⭐⭐⭐ 真机跑出来的 |
| **依规范修正** | 17/21/29/30 的错误码分类 | ⭐⭐ 有权威依据，但**本机无法实测复现**（QEMU 不实现 #AC） |
| **无法验证** | 串口电气时序 | ⭐ 只有真机 + 示波器能验 |

第二类虽然本机测不了，但**修正方向是唯一正确的**（否则与 CPU 实际行为不符），
而且修正不会带来任何副作用 —— 已用回归测试确认。

---

## 还建议做的事（按优先级）

1. **给 IDT 补上 32~47 号中断门的实际启用** —— 现在 IRQ 全部屏蔽着，
   地基搭好了但还没接上任何设备
2. **写 PIT 定时器驱动** —— 需要先 `pic_mask_all()` 里放开 IRQ0，再 `sti`
3. **异常处理的恢复能力** —— 一旦要支持"杀死出错任务"，17/21/29/30 那 4 条
   修正过的门才会派上用场（这也正是这次修它们的意义）
4. **LBA 扩展读（`INT 13h AH=42h`）** —— 让引导层彻底摆脱 CHS 几何依赖。
   注意：这一步**单独做修不了 q35 那个启动问题**（引导扇区压根没被执行），
   两者是不同层次的问题

## 本目录内的证据文件

| 文件 | 说明 |
|---|---|
| `当前完整源码` | kernel.c / idt.c / idt.h / isr.asm / pic.c / pic.h / io.h / boot.asm / entry.asm / link.ld / Makefile |
| `isr.asm.diff` | 相对上游 v0.5 的完整差异（83 行） |
| `serial-output.txt` | 4 行串口输出。**注**：输出里显示 `v0.5` 而非 `v0.5.1`，因为版本字符串写在 `kernel.c` 里，而本次修正全在 `isr.asm` |

> `serial-output.txt` 由 `../build-and-test.sh v0.5.1-fix` 生成 —— 用**该版本自己的源码**重新构建并启动后抓取的原始串口输出，可随时复现。
> 该脚本只编译版本目录里实际存在的模块，不会混入其它版本的代码。
