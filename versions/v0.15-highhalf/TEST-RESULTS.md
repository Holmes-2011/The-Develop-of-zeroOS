# v0.15-highhalf 测试记录

## 本版内容

**高半内核**：内核从虚拟地址 `0x100000` 搬到 `0xC0100000`，物理内存整体映射到 `0xC0000000` 起，
**0 ~ 3GB 的虚拟地址空出来留给用户态**（v0.16）。对用户的行为不变；这是 v0.13.2 复审时记下的
"用户态开工前必须做"的那一步。

| 改动 | 文件 |
|---|---|
| 链接到 `0xC0100000`，加载地址仍是物理 1MB（`AT(...)`） | `link.ld` |
| `elf_to_bin.py` 按加载地址排布，按物理末端检查 4MB 预算 | `elf_to_bin.py` |
| 入口：先清 `.bss`（物理地址），建一张临时页表把物理 0 ~ 4MB 同时映到虚拟 0 ~ 4MB 和 `0xC0000000` ~，开分页，绝对跳转到高地址，再装 GDT、换栈 | `entry.asm` |
| 分页：页目录 / 页表的物理地址和可访问指针分开（`P2V` / `V2P`）；`paging_init` 建"物理内存直接映射窗口"后换 CR3，临时表作废；低 3GB 一页不映射 | `paging.c` / `paging.h` |
| 常量：物理内存上限 3GB → **768MB**（窗口只有这么宽）；堆 `0xD0000000` → `0xF0000000`（128MB）；按需分页区 `0xC0000000` → `0xF8000000` | `pmm.h`、`heap.h`、`demand.h` |
| 所有碰物理页的地方改经 `P2V()`：页表访问、按需分页清零、物理页自检、显存、E820 地图、位图 | `pmm.c`、`demand.c`、`vga.c`、`memtest.c`、`shell.c` |
| `memtest` 新增 2 项：显存经窗口可达；`0xB8000` / `0x100000` 这些低地址**不再**映射 | `memtest.c` |
| 测试脚本跟上新地址：按需缺页事件的地址前缀 `0xC0` → `0xF8`；E820 对账只算 768MB 窗口以内 | `test-boot.sh` |

### 虚拟地址布局（`paging.h`）

```
0x00000000 ~ 0xBFFFFFFF  用户态（现在空着；第 0 页永远不映射）
0xC0000000 ~ 0xEFFFFFFF  物理内存直接映射：虚拟 = 物理 + 0xC0000000，最多 768MB；内核在 0xC0100000
0xF0000000 ~ 0xF7FFFFFF  内核堆
0xF8000000 ~ 0xFBFFFFFF  按需分页区
0xFC000000 ~ 0xFFFFFFFF  留着（memtest 用来测映射）
```

## 测试方法

`make` 构建后跑 `./test-boot.sh`（两阶段：第一阶段键盘路径，第二阶段 `bridge_test.py` 扮演宿主机
走 35 步协议）。矩阵覆盖机型、内存、CPU 三个维度；全部从**干净构建**开始，一次只跑一个 QEMU
实例（并行跑会让 100 毫秒采样和键盘时序互相干扰，见下方"已知限制"）。

## 测试结果

| 测试项 | 结果 |
|---|---|
| `make` | ✅ 无警告；核心代码无浮点 / SSE / cmov；物理占用 `0x100000 ~ 0x1200C0`，虚拟 = 物理 + `0xC0000000` |
| `-M pc` 32MB 全套（含桥接阶段） | ✅ **140/140**，协议 35/35 |
| `-M q35` 32MB 全套 | ✅ **135/135**，协议 35/35 |
| 486（无 SSE2） | ✅ **135/135**；`memtest` 26/26，浮点按预期关闭 |
| 2MB 内存（最低配置） | ✅ **136/136** |
| 1GB 内存（超过 768MB 窗口） | ✅ **140/140**；可用页 196313（≈767MB），正确截到窗口 |
| q35 + 4GB 内存 | ✅ **135/135**；E820 在 4GB 以上和 PCI 空洞处的内存被正确跳过 |
| 模拟用户 10 轮（`soak.sh`） | ✅ 0 异常、0 失败；`memtest` 10/10；90 次确认框；审计 251 条；空闲页回到初值（16027 → 16043） |

### 启动日志（pc，32MB）

```
zeroOS kernel v0.15: entered protected mode, kmain() started
zeroOS kernel v0.15: IDT installed, CPU exceptions now caught
zeroOS kernel v0.15: PIT at 100 Hz, IRQ0 enabled
zeroOS kernel v0.15: PS/2 keyboard on IRQ1 enabled
zeroOS kernel v0.15: FPU + SSE2 enabled
zeroOS kernel v0.15: clock source: TSC
zeroOS kernel v0.15: physical memory manager ready
zeroOS kernel v0.15: paging: higher-half kernel at 0xC0100000, low 3GB unmapped
zeroOS kernel v0.15: kernel heap ready at 0xF0000000
zeroOS kernel v0.15: telemetry sampling every 100 ms
zeroOS kernel v0.15: ATA disks found: 2
zeroOS kernel v0.15: shell ready on screen and COM1
zeroOS kernel v0.15: agent bridge listening on COM1 (protocol v1)
zeroOS kernel v0.15: tool layer ready, developer mode off
```

### 本版修掉的测试误报

| 问题 | 处理 |
|---|---|
| `events` 检查按 `demand fault x2 (0xC0…` 匹配，按需分页区搬到 `0xF8000000` 后对不上 | 改成 `0xF8` |
| E820 对账把可用内存截到 3GB（v0.14 的窗口），4GB 配置下比内核多算 300 多万页 | 截到 768MB（`0x30000000`） |

## 已知限制

| 项目 | 说明 |
|---|---|
| 物理内存上限 768MB | 直接映射窗口只有 768MB 宽；更大的机器只能用前 768MB（`mem` 会显示截断后的可用页） |
| 测试不能并行 | 100 毫秒采样、键盘时序、确认框超时都对 CPU 争用敏感；并行跑会出现假失败（本版实测过：486 / 2MB 在四路并行下各有 2~8 项误报，单独重跑全绿） |
| 没有在真机上跑过 | 所有结论来自 QEMU；真机的 UEFI / NVMe / USB 键盘另说 |

## 串口输出

见 [`serial-output.txt`](serial-output.txt)（键盘阶段）和 [`bridge-test.log`](bridge-test.log)（协议阶段）。
