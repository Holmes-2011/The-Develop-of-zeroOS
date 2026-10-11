# v0.10-float 测试记录

## 本版内容

开启浮点和 SSE2，补齐推理要用的数学函数，并实测运算速度——为在 zeroOS 里本机运行语言模型打基础
（项目决定见 [`docs/agent-design.md`](../../docs/agent-design.md)）。

| 改动 | 文件 |
|---|---|
| CPUID 检测（先确认 CPU 支持 CPUID）；支持 FPU + FXSR + SSE + SSE2 时开启：CR0 清 EM、置 MP/NE，CR4 置 OSFXSR/OSXMMEXCPT，`fninit` | `cpu.c` / `cpu.h`（新增） |
| 数学函数：`sqrtf` `expf` `logf` `powf` `sinf` `cosf` `tanhf` `floorf` `fabsf`，以及打印浮点数 | `kmath.c` / `kmath.h`（新增） |
| `memset` / `memcpy` / `memmove` / `memcmp` / `strlen`（编译器在裸机上也可能生成对它们的调用） | `kstring.c` / `kstring.h`（新增） |
| 新命令：`cpu`、`mathtest`（12 项）、`bench`（矩阵 × 向量速度 + 1B/3B 模型估算） | `floattest.c`、`bench.c`、`simd_test.asm`（新增）、`shell.c` |
| **代码分两组编译**：核心代码（含所有中断处理）连 x87 都禁掉；只有数学和推理代码允许 SSE2 | `Makefile` |
| 核心代码按 486 生成指令（不再用 `cmov`），构建时扫描确认 | `Makefile` |

### 为什么开了 SSE 却不在中断时保存 XMM 寄存器

zeroOS 现在只有一条执行流。中断处理程序属于核心代码，用 `-mno-sse -mno-80387` 编译，
根本不碰浮点 / XMM 寄存器，所以 SSE 计算被中断打断后，回来时寄存器原封不动。
这个前提有三道防线：编译器（核心代码写了浮点运算直接链接失败）、构建检查（扫描核心代码里的浮点指令）、
运行时测试（`mathtest` 实测 30 次中断后 xmm0~7 不变）。以后有了多任务，切换任务时必须 FXSAVE / FXRSTOR。

## 测试结果

| 测试项 | 结果 |
|---|---|
| `make` | ✅ 无警告；核心代码无浮点 / SSE / cmov；全内核无 AVX；`kstring.o` 里没有任何 call（`memset` 没被编译成调用自己） |
| `-M pc` 重复 | ✅ **10/10**（每次 43/43） |
| `-M q35` 重复 | ✅ **10/10** |
| 4GB 内存（pc / q35） | ✅ 43/43 |
| 4MB 内存 | ✅ `bench` 需要约 6MB，正确报"内存不足"并返回，其余全过 |
| `-cpu 486` / `pentium` / `pentium3`（没有 SSE2） | ✅ 39/39：正常启动，浮点自动关闭，`mathtest` / `bench` 提示不可用，`memtest` 照常通过 |

### 数学函数精度

和 Python 双精度 `math` 库算出的参考值比较（`gen_mathref.py` 生成 `mathref.h`），误差以 float 的机器精度 eps（2⁻²³）为单位：

| 函数 | 测试点 | 最大误差 | 门槛 |
|---|---|---|---|
| `sqrtf` | 12 | 0.19 eps | 1 |
| `expf` | 82（-87 ~ 88） | 0.56 eps | 4 |
| `logf` | 23（1e-30 ~ 3e38） | 0.47 eps | 4 |
| `sinf` | 45（含 12345.678） | 0.34 eps | 4 |
| `cosf` | 45 | 0.25 eps | 4 |
| `tanhf` | 44 | 0.30 eps | 4 |
| `powf` | 21（RoPE 的 10000^(-2i/d) 等） | 2.89 eps | 32 |

### `bench`：QEMU（Apple Silicon Mac 上软件模拟 x86）

| 权重格式 | 速度 | 1B 模型 | 3B 模型 |
|---|---|---|---|
| f32 | 约 80 M 次乘加 / 秒 | — | — |
| 8 位 | 约 64 M 次乘加 / 秒 | **约 15.5 秒 / 词** | 约 46.6 秒 / 词 |
| 4 位 | 约 63 M 次乘加 / 秒 | 约 15.9 秒 / 词 | **约 47.7 秒 / 词** |

只算矩阵运算，单核。QEMU 在非 x86 的 Mac 上逐条模拟 x86 指令，真机会快很多倍——但本机没有 x86 硬件，**真机速度没有实测**。

### 对照实验

| 实验 | 结果 |
|---|---|
| 中断入口里加一条 `pxor xmm0, xmm0` | ❌ 构建检查报"核心代码里有 1 条浮点 / SSE 指令"（正确地失败） |
| 同上，并绕过构建检查 | ❌ `mathtest` 11/12，"xmm0-7 survive 30 timer interrupts" 失败（正确地失败） |
| 核心代码（`pit.c`）里写一个浮点加法 | ❌ 链接失败：`undefined symbol: __addsf3`（正确地失败） |

### 测试中发现的问题

| 问题 | 怎么发现的 | 处理 |
|---|---|---|
| 不支持 CPUID 的老 CPU 上，`cpu_init` 执行 CPUID 会是非法指令 | 复查代码 | 先用 EFLAGS 的 ID 位判断 CPU 是否支持 CPUID |
| **已发布的 v0.6 ~ v0.9 在 486 / 初代 Pentium 上无法启动**：clang 默认按 Pentium 4 生成代码，用了 `cmov`（Pentium Pro 才有）| `-cpu 486` 实测崩在 `pit_init` 的 `cmovnel` | 核心代码改为 `-march=i486`，构建时扫描 `cmov`。对 1995 年以后的电脑没有影响 |
| `bench` 在 4MB 内存的机器上放不下 | 4MB 配置测试 | 设计如此：正确报错并返回；测试脚本改为在内存不足时检查"是否正确报错" |

### 无法在本机实测的部分

| 项目 | 说明 |
|---|---|
| 真机速度 | 没有 x86 硬件 |
| x87 / SSE 浮点异常 | MXCSR 保持默认（所有 SSE 浮点异常屏蔽），除零得 inf、0/0 得 nan，已实测；没有测试打开异常的情况 |

## 串口输出

见 [`serial-output.txt`](serial-output.txt)。
