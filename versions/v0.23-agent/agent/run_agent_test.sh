#!/usr/bin/env bash
# 起一个带串口 TCP 的 QEMU，跑脚本后端的 Agent，断言工具调用成功。
set -u
cd "$(dirname "$0")/.." || exit 1
QEMU="${QEMU:-$HOME/Documents/Harness WorkSpace/c-dev-apps/toolchain/qemu-i386}"
IMAGE="${IMAGE:-build/os-image.bin}"
PORT="${PORT:-5621}"
LOG="${LOG:-agent-test.log}"

"$QEMU" -L /Applications/UTM.app/Contents/Resources/qemu -M pc -m 64 \
    -drive format=raw,file="$IMAGE" \
    -serial tcp:127.0.0.1:"$PORT",server=on,wait=on \
    -display none -monitor none >/dev/null 2>&1 &
QP=$!
trap 'kill $QP 2>/dev/null' EXIT

sleep 0.3
OUT=$(python3 -m agent --port "$PORT" --task "check uptime and heap" --backend scripted \
    --plan '[{"tool":"get_uptime"},{"tool":"heap_check"},{"final":"agent-ok"}]' --verbose 2>&1)
echo "$OUT" | tee "$LOG"

fail=0
check() { if echo "$OUT" | grep -qF "$1"; then echo "[通过] $2"; else echo "[失败] $2"; fail=1; fi; }
check "get_uptime  -> ok" "Agent: get_uptime 调用成功"
check "uptime_ms=" "Agent: 拿到 uptime 输出"
check "heap_check  -> ok" "Agent: heap_check 调用成功"
check "agent-ok" "Agent: 最终回答"
echo "结果：$([ "$fail" -eq 0 ] && echo 全部通过 || echo 有失败)"
exit "$fail"
