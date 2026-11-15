# v0.23 测试记录

## 本版内容

宿主侧 Agent 循环 + 可插拔推理后端；桌面模式轮询桥接。

| 文件 | 作用 |
|---|---|
| `agent/link.py` | TCP/串口链路 |
| `agent/protocol.py` | 协议 v1 客户端 |
| `agent/loop.py` | Agent 循环 |
| `agent/backends/scripted.py` | 确定性计划后端 |
| `agent/backends/openai_http.py` | OpenAI 兼容后端 |
| `agent/run_agent_test.sh` | 起 QEMU + 断言 |

## 测试结果

| 测试项 | 结果 |
|---|---|
| `-M pc` 内核全套 | ✅ **166/166** |
| Agent：握手 + 工具清单 | ✅ |
| Agent：`get_uptime` / `heap_check` 调用 | ✅ |
| Agent：最终回答 | ✅ |
| 桌面模式（AUTO_DESKTOP=1）下调用 | ✅ |
| OpenAI 兼容后端 | 未起模型服务，仅代码路径 |

### 验证方法

```
qemu ... -serial tcp:127.0.0.1:5621,server=on,wait=on
python3 -m agent --port 5621 --backend scripted \
  --plan '[{"tool":"get_uptime"},{"tool":"heap_check"},{"final":"agent-ok"}]'
```

`run_agent_test.sh` 把这段自动化，输出 `结果：全部通过`。

## 已知限制

- 模型结构/权重尚未提供，用 scripted 后端验证链路。
- 只实现 hello/tools/call/result/event；verdict 和采样流解析未做。
- 确认框在桌面模式下不可见，破坏性工具暂不在桌面模式用。
