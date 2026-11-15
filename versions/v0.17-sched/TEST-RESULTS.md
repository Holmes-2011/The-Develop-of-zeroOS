# v0.17 测试记录

## 本版内容

**抢占式调度**：`run` 不再阻塞命令行，而是创建一个后台任务；时钟 tick 在任务间轮转，
shell 在等键盘时把 CPU 让出去，一有按键立刻回来。

| 改动 | 文件 |
|---|---|
| 任务表、轮转、抢占、僵尸回收、`sched_idle()` | `sched.c` / `sched.h` |
| `switch_to`（保存 / 恢复内核栈和 callee-saved 寄存器） | `sched_asm.asm` |
| 每个任务独立 8KB 内核栈；`TSS.esp0` 跟随当前任务 | `sched.c`、`gdt.c` |
| FXSAVE / FXRSTOR 保存浮点 / SSE 上下文（没有 fxsr 的 486 自动跳过） | `sched.c` |
| 用户程序改成位置无关：`call/pop` 取加载基址，每个任务 16MB 虚拟窗口 | `userprog.asm`、`userfault.asm`、`userloop.asm` |
| 系统调用：`exit` / ring 3 异常改为 `sched_exit()`，不再回到旧栈 | `usermode.c`、`usermode_asm.asm` |
| `run` 变成后台任务；新增 `jobs` / `kill` | `shell.c` |
| `wait_char` 空闲时调用 `sched_idle()` | `shell.c` |
| 测试：第五阶段（后台任务 + 命令行 + kill + 回收） | `test-boot.sh` |

### 调度策略

```
时钟 tick（10ms）
  └─ 当前是用户任务？
       ├─ 键盘缓冲区有输入 → 切回 shell（命令行优先）
       └─ 时间片用完（3 tick = 30ms）→ 轮转到下一个 ready 任务
内核（shell）不会被时钟抢占：它在 wait_char 里空闲时才调用 sched_idle() 让出 CPU。
```

## 测试结果

| 测试项 | 结果 |
|---|---|
| `-M pc` 32MB 全套 | ✅ **160/160**（151 + 9 项调度器） |
| `-M q35` 32MB | ✅ **155/155** |
| 486（无 SSE2 / 无 FXSR） | ✅ **155/155** |
| 2MB 内存 | ✅ **156/156** |
| 模拟用户 10 轮（`soak.sh`） | ✅ 0 异常、0 失败；`memtest` 10/10；空闲页回到初值 |

### 调度器阶段断言

| # | 断言 |
|---|---|
| 1 | `run loop` 返回后立刻有提示符（任务在后台） |
| 2 | 任务运行期间 `tick` 在输出 |
| 3 | 任务运行期间 `echo alive` 正常回显和执行 |
| 4 | `jobs` 列出 `loop`，状态 `ready` |
| 5 | 20 个 tick 后任务自己退出：`[task 1] loop exited, code 0` |
| 6 | `kill 2` 返回 `killed task 2` |
| 7 | 被杀任务被回收（`[task 2] loop killed (kill command)`） |
| 8 | `run fault` 的缺页任务被杀（`Page Fault`），shell 还活着 |
| 9 | 所有任务结束后 `memtest` 26/26（页面 / 页表都收回来了） |

## 已知限制

| 项目 | 说明 |
|---|---|
| 内核不抢占 | 只有用户任务被时钟打断；shell 在执行命令时后台任务暂停（命令都很短，可接受） |
| 输出会交错 | 没有 per-task 控制台；后台任务和命令行共用屏幕 / 串口，打字时能看到 tick 插在中间 |
| 没有优先级 / 阻塞 | 纯轮转；`sleep` 仍占着 CPU（只是 hlt），以后可以改成阻塞队列 |
| `kill` 有延迟 | 置为僵尸后，要等下一次调度才会切走；资源在 shell 空闲时回收 |
| 单核、无 SMP | 没有锁的考虑（现在也只有一个 CPU） |

## 串口输出

见 [`serial-output.txt`](serial-output.txt) 和 [`bridge-test.log`](bridge-test.log)。
