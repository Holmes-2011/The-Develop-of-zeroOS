# v0.5-pic — 测试记录

## 这一版是什么

上游标注的 **v0.5**。新增 **8259 PIC 重映射 + IRQ0~15 入口**。

| 文件 | 大小 | 说明 |
|---|---|---|
| `upstream/kernel.c` | 5045 | 加了 PIC 重映射调用 + `isr_handler` 区分异常/硬件中断 |
| `upstream/idt.c` | 5110 | IDT 门扩展到 0~47（16 个 IRQ 门） |
| `upstream/isr.asm` | 2803 | 新增 `irq0`~`irq15` 入口桩（中断号压 32~47） |
| `upstream/pic.c` | 1344 | `pic_remap` / `pic_send_eoi` / `pic_mask_all` |
| `upstream/pic.h` | 827 | PIC 接口声明 |
| `upstream/io.h` | 499 | `inb` / `outb` 公共函数 |
| `upstream/test-boot.sh` | 2018 | **上游自带的自动化测试脚本** |
| `Makefile.linux` | 1594 | 上游的 Linux 版 Makefile |
| `README-upstream.md` | 5621 | 上游原版 README |
| `upstream-source.zip` | 12917 | **上游原始压缩包**（完整 9 个文件） |

## 这一版的设计意图

把硬件中断号从 BIOS 默认的 0~15 **挪到 32~47**，避开和 CPU 异常号（0~31）撞车
（比如 IRQ0 定时器默认会撞上 8 号"双重故障"异常）。

**所有 IRQ 仍然全部屏蔽、IF 也没打开** —— 运行行为和 v0.4 完全一样，
这一步只是把地基搭好，为以后写定时器/键盘驱动做准备。

---

## 测试项与结果

### ① 基本启动 ✅

```
zeroOS kernel v0.5: entered protected mode, kmain() started
zeroOS kernel v0.5: IDT installed, CPU exceptions now caught
zeroOS kernel v0.5: PIC remapped to 32-47, all IRQs masked
zeroOS kernel v0.5: screen cleared, entering halt loop
```

四行全部出现，`pc` 和 `q35` 两种机型都通过。

### ② PIC 重映射是否真的生效 ✅

**IDT 门 32~47**（从内存里读出真实的描述符解码）：

| 向量 | 偏移 | flags | 对应符号 |
|---|---|---|---|
| 32 | `0x017FB` | `0x8E` | `irq0` |
| 33 | `0x01802` | `0x8E` | `irq1` |
| … | …（中间 13 个地址逐个递增） | `0x8E` | … |
| 47 | `0x01864` | `0x8E` | `irq15` |

16 个门全部注册，地址和符号表里的 `irq0`~`irq15` **逐个对上**，
门类型 `0x8E` = 存在 + ring0 + 32 位中断门。

**PIC 屏蔽寄存器**（直接读 I/O 端口）：

```
主片 0x21 = 0xff
从片 0xA1 = 0xff
```

两条线全 `0xFF`，所有 16 条 IRQ 确实被屏蔽 ✓

### ③ 全部 32 个 CPU 异常向量逐个触发 ✅ **32 / 32**

每个向量单独编译一个内核、单独启动一次，核对 `isr_handler` 报出的名字：

```
 0  Division By Zero            ✓     16  Coprocessor Fault        ✓
 1  Debug                       ✓     17  Alignment Check          ✓
 2  Non Maskable Interrupt      ✓     18  Machine Check            ✓
 3  Breakpoint                  ✓     19  Reserved                 ✓
 4  Into Detected Overflow      ✓     20  Reserved                 ✓
 5  Out of Bounds               ✓     21  Reserved                 ✓
 6  Invalid Opcode              ✓     22~31 Reserved               ✓
 7  No Coprocessor              ✓
 8  Double Fault                ✓
 9  Coprocessor Segment Overrun ✓
10  Bad TSS                     ✓
11  Segment Not Present         ✓
12  Stack Fault                 ✓
13  General Protection Fault    ✓
14  Page Fault                  ✓
15  Unknown Interrupt           ✓
```

### ④ 真实硬件异常（不是软件 `int n`）✅

测试方法：把非法段选择子 `0x46` 装进 `ds`，让 **CPU 自己**产生 #GP。

```
CPU exception -> General Protection Fault  err=0x00000044  eip=0x00001DD1  cs=0x00000008
```

- `err=0x44` —— 选择子相关的错误码（非零，说明错误码确实被读到了）
- `eip=0x1DD1` —— 一段真实的代码地址（触发指令的位置）
- `cs=0x08` —— 内核代码段

**三个字段全部正确**，证明 `ISR_ERRCODE` 的栈布局对真实异常是对的。

### ⑤ 上游 `test-boot.sh` ✅

上游自带的脚本原样在 macOS 上**跑不了**（用了 `timeout` 和 `qemu-system-i386`，
这两个在 macOS 上都不存在）。我把它改成**两个平台都能用**：

- QEMU：优先用 PATH 里的 `qemu-system-i386`，找不到就用工具链里的启动器
- 超时：有 `timeout` 就用，没有就自己实现（后台启动 + 轮询 + kill，还能提前命中就退出）

改完后在 macOS 上通过：

```
[通过] 出现: entered protected mode, kmain() started
[通过] 出现: IDT installed, CPU exceptions now caught
[通过] 出现: PIC remapped to 32-47, all IRQs masked
[通过] 出现: screen cleared, entering halt loop
结果：全部通过
```

---

## 这一版查出的缺陷

### 17 / 21 / 29 / 30 号异常的错误码分类错误 ❌

`isr.asm` 把 4 个向量标成了 `ISR_NOERRCODE`，但 CPU 真实触发这些异常时**会压错误码**。

**依据**：`x86_64` crate 的 IDT 定义（注释标明来源 *Intel SDM Vol.3A 6.3.1 / AMD APM Vol.2 8.4.2*），
用类型区分：

```rust
alignment_check:               Entry<HandlerFuncWithErrCode>  // 17 #AC
cp_protection_exception:       Entry<HandlerFuncWithErrCode>  // 21 #CP
vmm_communication_exception:   Entry<HandlerFuncWithErrCode>  // 29 #VC
security_exception:            Entry<HandlerFuncWithErrCode>  // 30 #SX
```

完整的错误码向量表共 **10 个**：`8, 10, 11, 12, 13, 14, 17, 21, 29, 30`。

| 后果 | 说明 |
|---|---|
| **当前** | **无害** —— `isr_handler` 遇到异常就 `while(1) hlt` **永不返回**，那条 `iret` 到不了 |
| **将来** | 一旦给异常处理加上"恢复现场/杀掉任务"的能力，这 4 个会从错位 4 字节的栈上弹返回地址 → 跳飞 |

已在 **v0.5.1** 修掉（见 `../v0.5.1-fix/`）。

---

## 测试方法上的一个坑（记录备查）

用 `int $0xNN` 触发异常时，**CPU 不会压错误码**（不管那个向量正常情况下有没有）。
所以拿 `int $0x0D` 去测 #GP 的错误码路径，测到的是错位后的垃圾值。

**要测错误码路径，必须用真实异常**（如非法段选择子触发真 #GP）。

这个坑一开始让我把 `eip=0x08` 当成了"布局错误"，实际是测试方法不对。

## 未能实测的项目（诚实标注）

| 项目 | 原因 |
|---|---|
| 17 号 #AC 对齐检查 | **QEMU 未实现该功能** —— 设了 `CR0.AM` + `EFLAGS.AC` 后做非对齐访问，`-cpu max`/`Nehalem`/`qemu32`/`pentium3` 四种型号都触发不出 |
| 串口电气时序 | 波特率误差、FIFO 溢出这类只有**真机 + 示波器**能验 |

## 本目录内的证据文件

| 文件 | 说明 |
|---|---|
| `upstream/` | 该版本全部源码（kernel.c / idt.c / isr.asm / pic.c / pic.h / io.h / test-boot.sh） |
| `Makefile.linux / README-upstream.md` | 上游的 Linux 版构建脚本与说明 |
| `upstream-source.zip` | **上游原始压缩包**（12917 字节，9 个文件） |
| `serial-output.txt` | 4 行串口输出 |

> `serial-output.txt` 由 `../build-and-test.sh v0.5-pic` 生成 —— 用**该版本自己的源码**重新构建并启动后抓取的原始串口输出，可随时复现。
> 该脚本只编译版本目录里实际存在的模块，不会混入其它版本的代码。
