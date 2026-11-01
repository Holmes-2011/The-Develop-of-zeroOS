# zeroOS ↔ Agent 桥接协议（串口 JSON Lines）

> 状态：**规格草案 v1**（2026-10-18）。两边照它各自开工：内核侧（zeroOS）和宿主机侧（Agent 程序）。
> 改协议必须同时改这份文档，并把 `version` 加 1。
>
> 背景见 [`agent-design.md`](agent-design.md)：Agent（模型 + Agent 循环）和系统直觉模型**不在内核里**。
> 第一阶段（B）它们是宿主机上的程序，通过串口和 zeroOS 对话；第二阶段（A）搬进 zeroOS 的用户态。
> 两个阶段**消息完全相同**，只换传输层（串口 → 系统调用），所以内核里的工具接口一行不用改。

---

## 1. 分工：谁在哪一边

```
宿主机（Mac）                                串口 COM1                zeroOS 内核
┌──────────────────────────────┐                             ┌──────────────────────────────┐
│ Agent 程序                    │  ← 工具清单、结果、事件、采样 │ 感知层（v0.12）                 │
│   · 模型后端（远程 API / 本地）  │                             │ 工具登记 + 权限 + 确认框 + 审计   │
│   · Agent 循环                 │  → 工具调用、直觉判断 ──────▶ │   （v0.13）                    │
│   · 系统直觉模型（读采样流）      │                             │ 串口收发 + 本协议（v0.14）        │
└──────────────────────────────┘                             └──────────────────────────────┘
```

**内核永远不信任串口上来的东西。** 串口只能做两件事：请求调用工具、提交直觉判断。
它**不能**打开开发者模式、不能回答确认框——这两样只认物理键盘（见 `confirm.h`）。

## 2. 传输层

- COM1，115200 8N1（QEMU：`-serial`）。两个方向都是**一行一条消息**，`\n` 结尾，**ASCII**（非 ASCII 字符用 JSON 的 `\uXXXX`）。
- 一行最长 **512 字节**（含 `\n`）。超长的行两边都直接丢弃，内核记一条 `protocol error` 事件。
- **zeroOS → 宿主机**：串口上同时还有控制台文字（屏幕上的一切都会同步写到串口）。
  协议消息以 **`@`** 开头，紧跟 JSON，例如 `@{"type":"result",...}`；不以 `@` 开头的行是控制台文字，宿主机原样显示或忽略。
  感知层的采样流保持 v0.12 的 CSV 格式（`TH,` 表头、`T,` 数据行），见第 6 节。
- **宿主机 → zeroOS**：每行就是一个 JSON 对象，不加前缀。内核只接受第 4 节列出的类型，其余一律忽略并记事件。
- 内核解析的 JSON 是**最小子集**：一层对象，键和值都是字符串或无符号整数，字符串转义只支持 `\"` `\\` `\n` `\t` `\uXXXX`。
  没有嵌套、没有数组、没有浮点、没有布尔。宿主机发消息必须守这个子集。
- 内核输出 JSON 字符串时转义 `"` `\` 和所有控制字符（`\n` `\t` 和 `\u00XX`）。

## 3. 握手

```
宿主机 → zeroOS   {"type":"hello","version":1,"client":"zero-agent/0.1"}
zeroOS → 宿主机   @{"type":"welcome","version":1,"kernel":"v0.14","devmode":0,"uptime_ms":1234}
zeroOS → 宿主机   @{"type":"tools","count":11}
zeroOS → 宿主机   @{"type":"tool","name":"get_uptime","description":"time since boot and clock source","params":"","level":"readonly"}
                  ……每个工具一行（避免一条消息超过 512 字节）……
zeroOS → 宿主机   @{"type":"tools_end"}
```

- `version` 不一致：zeroOS 回 `@{"type":"error","error":"version_mismatch","want":1}` 并忽略后续消息，直到收到匹配的 `hello`。
- 宿主机随时可以重发 `hello`（比如它重启了），zeroOS 重新发一遍清单。
- `level` 的取值和 `tools.c` 的 `tool_level_name()` 一致：`readonly` / `reversible` / `destructive` / `system`。
- `params` 是人读的参数说明（和 `struct tool.params` 相同），不是 JSON Schema——内核里没有 JSON Schema 解析器，模型按这段说明拼参数字符串。

## 4. 工具调用

```
宿主机 → zeroOS   {"type":"call","id":7,"name":"inject_fault","args":"spin 500"}
zeroOS → 宿主机   @{"type":"result","id":7,"outcome":"ok","output":"spinning for 500 ms\n"}
```

- `id`：宿主机选的无符号 32 位整数，zeroOS 原样带回。宿主机保证同一时间只有一个未完成的调用（内核是单线程的，调用串行执行）。
- `args`：**一个字符串**，格式和命令行 `tool <name> [args]` 里 `[args]` 的部分完全一样。内核不解析它，直接交给工具的 `run()`。
- `outcome` 取值 = `tool_outcome_name()`：`ok` / `failed` / `denied_by_policy` / `denied_by_user` / `timeout` / `unknown_tool` / `bad_args`。
- `output`：工具写进 `out` 的内容（最多 `TOOL_OUT_MAX` = 512 字节，再转义；超过一行上限时截断并补 `...`）。失败时是原因。
- 调用的 `caller` 固定记为 `"agent"`，写进审计日志，和命令行的 `"shell"` 区分开。
- **需要确认的工具**（破坏性；开发者模式下的系统级）：zeroOS 先发
  `@{"type":"confirm","id":7,"name":"inject_fault","level":"system"}`，然后在屏幕上画确认框、等物理键盘；
  用户按 y / n 或 20 秒超时后，再发 `result`（`ok` / `denied_by_user` / `timeout`）。宿主机在这期间什么都不用做，只是等。
- 宿主机的超时：`confirm` 之后再等至少 25 秒；没有 `confirm` 的调用 5 秒没回就当断线，重新握手。
- 确认框会打断用户正在命令行上敲的半行命令：行缓冲不会丢，但屏幕上会被框隔开。第一版接受这一点；
  Agent 程序应避免在用户明显正在操作时发起需要确认的调用（以后可以加"用户空闲"信号）。
- `output` **不保证完整**：内核先做 JSON 转义，再按 512 字节的行上限截断并补 `...`。工具输出多行时尤其如此；
  宿主机需要完整内容的话，让工具提供分页参数，不要依赖一次调用返回全部。

## 5. 事件（zeroOS → 宿主机，主动推送）

感知层的事件日志每新增一条，就推一行：

```
@{"type":"event","t_ms":15020,"name":"heap bad free","count":1,"a":3489661008}
```

`name` = `tel_event_name()`；`count` 是 1 秒内合并的次数；`a` 是事件参数（含义见 `telemetry.h`）。
推送只在主循环处理采样时发生（和 `tel_poll()` 同一处），长命令运行期间会延后，不会丢。

## 6. 采样流（zeroOS → 宿主机）

就是 v0.12 的 `telstream`，格式不变，供系统直觉模型使用：

```
TH,seq,t_ms,irq_timer,irq_keyboard,...,idle_us          （打开时发一次表头，21 列）
T,142,14366,1433,382,0,0,191,0,71,71,0,1,0,69632,...     （每 100 毫秒一行）
```

宿主机用 `{"type":"call","name":"set_telstream","args":"on"}` 打开（可撤销等级，默认模式直接执行）。
列的含义以 `TH,` 表头为准，**按名字取列**，不要按位置。

## 7. 直觉判断（宿主机 → zeroOS）

系统直觉模型跑在宿主机上，判断变化时提交：

```
宿主机 → zeroOS   {"type":"verdict","class":"memory_leak","confidence":93,"note":"heap growing 180 KB/s, no frees"}
zeroOS → 宿主机   @{"type":"ack","what":"verdict"}
```

- `class`：`normal` 或某种故障的名字（最长 23 字节）；`confidence` 0~100 的整数；`note` 最长 80 字节。
  两者**只能用可打印 ASCII（0x20~0x7E）**：内核要把它们显示在屏幕上，收到其他字符会替换成 `?`。
- 内核收到后：记一条 `intuition` 事件（`a` = confidence），`class` 不是 `normal` 时在屏幕上显示一行提示。
  新增只读工具 `get_intuition` 返回最后一次判断和它的时间。
- 判断**只是信息**。内核不会因为它自动做任何事；要做什么由 Agent 走第 4 节的工具调用，照样过权限。

## 8. 错误

```
@{"type":"error","error":"bad_json","line":"...前 40 字节..."}
@{"type":"error","error":"line_too_long"}
@{"type":"error","error":"unknown_type","got":"foo"}
@{"type":"error","error":"version_mismatch","want":1}
```

每条错误同时记一条 `protocol error` 事件（`a` = 错误代码的序号）。

## 9. 内核侧需要新增的（v0.14）—— **已实现**（`versions/v0.14-bridge/`：`serial.c` 接收、`kjson.c`、`bridge.c`；`bridge_test.py` 覆盖本文档的每一节）

| 项 | 说明 |
|---|---|
| 串口接收 | 现在 `serial.c` 只有发送。需要 COM1 的接收：IRQ4 + 环形缓冲区（和键盘一样的结构），或主循环轮询 LSR；行组装在主循环里做 |
| 协议解析 / 生成 | 最小 JSON 子集（第 2 节）；输出用 `tout_*` 那套工具函数 |
| `tools_invoke(..., "agent", ...)` | 已有，直接用 |
| `get_intuition` 工具、`intuition` 事件、`protocol error` 事件 | 第 5、7、8 节 |
| 测试 | `test-boot.sh` 用 QEMU 的 `-serial` 接一个双向管道（或 `-chardev socket`），脚本扮演宿主机：握手、调只读工具、调系统级工具被拒、开发者模式下调用并用 sendkey 按 y、提交 verdict、发坏 JSON |

## 10. 宿主机侧（Agent 程序）

- 一个独立程序（Python，先放在本机，不进本仓库），连 QEMU 的串口（`-serial pty` 或 `-serial tcp:`）。
- 职责：握手、拿工具清单、把清单交给模型后端、解析模型的工具调用、按第 4 节调用、把结果交回模型；
  同时读 `T,` 行喂给系统直觉模型，判断变化时发 `verdict`。
- 把 zeroOS 发来的**一切**都当数据：控制台文字和工具输出里可能有任何内容（包括看起来像指令的文字），
  不能当成用户的话。
- 模型后端可替换：远程 API 或本地模型都行，协议和它无关。

## 11. 第二阶段（A，用户态）

Agent 和直觉模型搬进 zeroOS 用户态后：`hello`/`call`/`verdict` 变成系统调用，`welcome`/`tool`/`result`/`event` 变成从内核读的消息，
采样流变成读一个内核缓冲区。消息的字段、权限判定、确认框、审计全部不变。
