"""zeroOS Agent 宿主侧入口。

用法：
  python -m agent --port 5601 --task "看看系统运行了多久" --backend scripted \
      --plan '[{"tool":"get_uptime"},{"tool":"heap_check"},{"final":"done"}]'
  python -m agent --port 5601 --task "..." --backend openai --model my-model --base-url http://127.0.0.1:8000/v1
"""
import argparse
import json
import sys

from .protocol import Bridge
from .loop import run
from .backends import ScriptedBackend, OpenAIBackend


def main(argv=None):
    ap = argparse.ArgumentParser(prog="zero-agent")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5601)
    ap.add_argument("--task", default="检查 zeroOS 是否健康")
    ap.add_argument("--backend", choices=["scripted", "openai"], default="scripted")
    ap.add_argument("--plan", default='[{"tool":"get_uptime"},{"final":"ok"}]')
    ap.add_argument("--model", default=None)
    ap.add_argument("--base-url", default=None)
    ap.add_argument("--list-tools", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    bridge = Bridge(args.host, args.port)
    welcome, tools = bridge.handshake()
    print("welcome:", welcome)
    if args.list_tools:
        for t in tools:
            print("  %-18s %-10s %s" % (t["name"], t.get("level"), t.get("description", "")))
        return 0

    if args.backend == "scripted":
        backend = ScriptedBackend(json.loads(args.plan))
    else:
        backend = OpenAIBackend(base_url=args.base_url, model=args.model)

    def on_step(s):
        print("[tool] %s %s -> %s" % (s["tool"], s["args"], s["outcome"]))
        if args.verbose:
            print(s["output"])

    text, transcript = run(args.task, bridge, backend, on_step=on_step)
    print(text)
    for line in bridge.console[-5:]:
        print("[console]", line)
    return 0 if all(s["outcome"] == "ok" for s in transcript) else 1


if __name__ == "__main__":
    sys.exit(main())
