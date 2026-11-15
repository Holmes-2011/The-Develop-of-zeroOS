# zeroAgent —— zeroOS 的宿主侧 Agent 循环（阶段 B）

> 设计见 `docs/agent-design.md`，协议见 `docs/agent-bridge-protocol.md`（v1）。
> 模型和 Agent **不在内核里**；这一层跑在宿主机，通过串口 JSON Lines 调用 zeroOS 的工具。

```
宿主（现在）                                  zeroOS 内核
┌─────────────────────────────┐   COM1/TCP   ┌──────────────────────────┐
│ agent loop（本目录）          │  JSON Lines  │ 感知层 / 工具层 / 权限     │
│  ├─ backend：脚本 / OpenAI 兼容 │ ◀──────────▶ │ 确认框 / 审计             │
│  └─ 工具调用 + 结果回填         │              │ 桥接协议 v1               │
└─────────────────────────────┘              └──────────────────────────┘
```

## 快速开始

```bash
# 在 zeroOS 仓库根目录，先起一个带串口 TCP 的 QEMU（wait=on，先连后启动）
qemu-system-i386 -M pc -m 64 -drive format=raw,file=versions/v0.23-agent/build/os-image.bin \
  -serial tcp:127.0.0.1:5621,server=on,wait=on -display none -monitor none &

# 确定性脚本后端：调用 get_uptime、heap_check
cd versions/v0.23-agent
python3 -m agent --port 5621 --backend scripted \
  --plan '[{"tool":"get_uptime"},{"tool":"heap_check"},{"final":"ok"}]' --verbose
```

输出：

```
welcome: {'type': 'welcome', 'version': 1, 'kernel': 'v0.23', ...}
[tool] get_uptime  -> ok
uptime_ms=...
[tool] heap_check  -> ok
ok blocks=1
ok
```

## 接你自己的模型

任何暴露 **OpenAI 兼容** `/v1/chat/completions`（含函数调用）的服务都能接：

```bash
python3 -m agent --port 5621 --backend openai \
  --base-url http://127.0.0.1:8000/v1 --model my-model \
  --task "看看系统运行了多久，堆有没有问题"
```

- `ZERO_AGENT_BASE_URL` / `ZERO_AGENT_MODEL` / `ZERO_AGENT_API_KEY` 也可用环境变量。
- 模型的工具参数是一个**字符串**（和 `tool <name> [args]` 一样）；约定模型输出 `{"args":"..."}`，或键值对拼成 `k=v`。
- 模型换了、量化方式换了，这一层不用改；只改后端。

## 目录

| 文件 | 作用 |
|---|---|
| `link.py` | TCP/串口链路，一行一条；`@` 开头是协议 JSON |
| `protocol.py` | 握手、工具清单、`call`/`result`、重复 hello |
| `loop.py` | Agent 循环：模型 → 工具 → 结果 → 模型 |
| `backends/scripted.py` | 确定性计划，用于测试/演示 |
| `backends/openai_http.py` | OpenAI 兼容后端（本地/远程模型） |
| `run_agent_test.sh` | 起 QEMU + 跑脚本后端 + 断言 |

## 以后搬进 zeroOS（阶段 A）

`loop.py` 只依赖 `Bridge.call()` / `Bridge.tools`。搬进 zeroOS 用户态时：

- 传输：串口 → `int 0x80` 系统调用（`agent_call(name, args)`）；
- 后端：宿主进程 → zeroOS 用户态的推理引擎（积木式，见设计文档）；
- 权限、确认框、审计、工具清单**完全不变**。

## 已知限制

- 只实现了 v1 协议里的 hello / tools / call / result / event 读取；`verdict` 和采样流解析还没做。
- 需要确认的工具会等内核的确认框；自动化测试里不要调用破坏性工具。
- 桌面模式下桥接由桌面循环轮询（v0.23 起），所以开机进桌面时 Agent 仍可用。
