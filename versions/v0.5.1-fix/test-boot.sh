#!/bin/bash
# test-boot.sh —— 自动化引导测试，而不是人工盯着 QEMU 窗口看
#
# 这是把"审查代码"升级成"可重复测试"的第一步：以后每次改完代码，
# 跑一下这个脚本，就知道有没有把之前跑通的东西改坏——这正是 Linux 内核
# 持续集成那套逻辑的最小化版本：不是靠审查相信代码是对的，是靠反复跑验证它是对的。
#
# 用法：先 `make` 编译出 build/os-image.bin，再 `./test-boot.sh`
#
# ── 跨平台说明 ─────────────────────────────────────────────
# 原版假设在 Linux/Kali 上跑（qemu-system-i386 + timeout 命令）。
# macOS 上没有 timeout，也没有 qemu-system-i386，所以这里做了自动适配：
#   * QEMU：优先用 PATH 里的 qemu-system-i386；找不到就用工具链里的启动器
#           （~/Documents/Harness WorkSpace/c-dev-apps/toolchain/qemu-i386，
#            它把 UTM 内置的 QEMU 封装成了命令行程序）
#   * 超时：有 timeout 就用，没有就用"后台启动 + 轮询 + kill"自己实现
set -u

IMAGE="${1:-build/os-image.bin}"
LOG="boot-test.log"
TIMEOUT=5   # 秒。正常情况下内核几乎瞬间就跑到 halt 循环了，5秒绰绰有余

CDEV="${CDEV:-$HOME/Documents/Harness WorkSpace/c-dev-apps}"
LAUNCHER="$CDEV/toolchain/qemu-i386"
BIOS_DIR="/Applications/UTM.app/Contents/Resources/qemu"

if [ ! -f "$IMAGE" ]; then
    echo "错误：找不到 $IMAGE，先运行 make"
    exit 1
fi

# ---- 挑一个能用的 QEMU ----
QEMU=""
EXTRA_ARGS=()
if command -v qemu-system-i386 >/dev/null 2>&1; then
    QEMU="$(command -v qemu-system-i386)"
elif [ -x "$LAUNCHER" ]; then
    QEMU="$LAUNCHER"
    EXTRA_ARGS=(-L "$BIOS_DIR")     # 让 QEMU 找到 BIOS 固件
else
    echo "错误：既没有 qemu-system-i386，也找不到 $LAUNCHER"
    echo "      装一个 QEMU（brew install qemu），或先构建工具链里的启动器"
    exit 1
fi

echo "QEMU: $QEMU"
echo "启动 QEMU（无图形界面，超时 ${TIMEOUT}s 自动终止）..."
rm -f "$LOG"

if command -v timeout >/dev/null 2>&1; then
    timeout "$TIMEOUT" "$QEMU" "${EXTRA_ARGS[@]}" \
        -drive format=raw,file="$IMAGE" \
        -serial file:"$LOG" \
        -display none \
        || true   # timeout 杀掉 qemu 会返回非0，这是预期行为，不算脚本失败
else
    # macOS 没有 timeout，自己实现一份：跑起来后轮询串口日志，
    # 拿到最后一行就提前结束，不用干等满超时
    "$QEMU" "${EXTRA_ARGS[@]}" \
        -drive format=raw,file="$IMAGE" \
        -serial file:"$LOG" \
        -display none >/dev/null 2>&1 &
    QPID=$!
    n=0
    while [ "$n" -lt $((TIMEOUT * 10)) ]; do
        grep -qF "entering halt loop" "$LOG" 2>/dev/null && break
        sleep 0.1
        n=$((n + 1))
    done
    kill "$QPID" 2>/dev/null
    wait "$QPID" 2>/dev/null || true
fi

echo "---- 串口输出 ----"
cat "$LOG" 2>/dev/null || echo "(没有任何输出)"
echo "-------------------"

# 期待看到的几行关键日志，对应 v0.5 kmain() 里实际打印的内容——
# 以后内核行为变了，这里要跟着改，别让测试脚本和代码脱节
EXPECTED_LINES=(
    "entered protected mode, kmain() started"
    "IDT installed, CPU exceptions now caught"
    "PIC remapped to 32-47, all IRQs masked"
    "screen cleared, entering halt loop"
)

PASS=1
for line in "${EXPECTED_LINES[@]}"; do
    if grep -qF "$line" "$LOG" 2>/dev/null; then
        echo "[通过] 出现: $line"
    else
        echo "[缺失] 没出现: $line"
        PASS=0
    fi
done

echo ""
if [ "$PASS" -eq 1 ]; then
    echo "结果：全部通过"
    exit 0
else
    echo "结果：有缺失。串口日志停在哪一行，问题大概率就在那一行之后的代码——"
    echo "      对照 boot.asm / kernel.c 里对应位置的注释定位"
    exit 1
fi
