# v0.16 — 用户态：ring 3 + 系统调用（当前版本）

第一批 ring 3 程序跑起来了。内核里嵌了一个演示程序和一个故意犯错的程序，`run` 启动：

```
zeroOS> run
[user] hello: entering ring 3
hello from ring 3
uptime: 2010 ms
guard: kernel address rejected
tool get_uptime -> 0: uptime_ms=2071 clock=tsc timer_irqs=203
[user] exit code 0

zeroOS> run fault
[user] fault: entering ring 3
about to touch kernel memory
[user] program killed: Page Fault at EIP 0x00400011
zeroOS>
```

从这一版起，**程序不再和内核混在一个特权级里**：

- 程序只能用 `int 0x80` 进内核（GDT 里 0x80 号门是唯一 DPL=3 的门）。
- 内核页在页表里没有 USER 位，程序读 `0xC0000000` 会直接缺页。
- 程序里出任何 CPU 异常，内核只杀掉它、回到 shell——**不再整机停机**。

## 改动

| # | 改动 | 说明 |
|---|---|---|
| ① | GDT 扩到 6 项 + TSS | ring 3 代码 / 数据段（DPL=3）、32 位 TSS；`esp0` 指向独立的 8KB 系统调用栈（不能借主循环的栈） |
| ② | `int 0x80` 系统调用 | 门 DPL=3；6 个调用：`exit` / `write` / `getkey` / `uptime` / `tool` / `sleep` |
| ③ | 用户内存 | 代码 4MB 起、栈 8MB 顶；`pmm_alloc_page` + `paging_map(..., PAGE_USER)`；程序退出 / 被杀后全部释放 |
| ④ | 用户指针校验 | 系统调用里的指针先查范围、再逐页查 USER 位；不合法返回 -1，内核不替用户态碰非法地址 |
| ⑤ | ring 3 异常处理 | `isr_handler` 先分 `int 0x80`，再判断「异常号 < 32 且 CS.RPL=3」→ 杀程序；硬件中断（32~47）从 ring 3 来不算故障 |
| ⑥ | 内嵌用户程序 | `userprog.asm`（演示）、`userfault.asm`（故意读内核地址）；`userbin.asm` 用 `incbin` 嵌进内核 |
| ⑦ | `run` 命令 | `run` / `run fault` |

### 系统调用表

| 号 | 名字 | 参数 | 返回 |
|---|---|---|---|
| 1 | `exit` | `ebx` = 退出码 | 不返回，进程结束 |
| 2 | `write` | `ebx` = buf，`ecx` = len | 实际写出的字节数；指针非法返回 -1 |
| 3 | `getkey` | — | 字符；没有按键返回 -1 |
| 4 | `uptime` | — | 开机毫秒数 |
| 5 | `tool` | `ebx` = name，`ecx` = args，`edx` = out，`esi` = cap | 工具结果码（0 = ok）；审计里 `caller = "user"` |
| 6 | `sleep` | `ebx` = 毫秒 | 0 |

## 验证

| 配置 | 结果 |
|---|---|
| `-M pc` | ✅ **151/151**（143 + 7 项用户态 + 1 项 help） |
| `-M q35` | ✅ 146/146 |
| 486（无 SSE2） | ✅ 146/146 |
| 2MB 内存 | ✅ 147/147 |
| 模拟用户 10 轮 | ✅ 0 异常 0 失败 |

用户态阶段断言：打印 `hello from ring 3`、`uptime: N ms`、内核地址被拒绝、`tool get_uptime -> 0`、`exit code 0`、缺页程序被杀死且回到 shell、跑完后 `memtest` 26/26。

## 已知限制

| 项目 | 说明 |
|---|---|
| 只有一个用户程序 | 没有任务切换 / 调度（v0.17），一次只能跑一个，跑的时候 shell 等它结束 |
| 用户栈只有一页 4KB | 演示够用；递归深了会缺页被杀（不是崩内核） |
| 没有系统调用号表检查之外的保护 | 用户程序可以死循环，只能等它自己退出；抢占要等 v0.17 |
| 用户程序内嵌在核心里 | 以后要做文件系统 / ELF 加载，程序才能独立存放 |
| 真机没验证 | 结论来自 QEMU（pc / q35 / 486 / 2MB） |

## 串口输出

见 [`serial-output.txt`](serial-output.txt) 和 [`bridge-test.log`](bridge-test.log)。
