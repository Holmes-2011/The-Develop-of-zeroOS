#!/bin/bash
# test-boot.sh —— 自动化引导测试，而不是人工盯着 QEMU 窗口看
#
# 这是把"审查代码"升级成"可重复测试"的第一步：以后每次改完代码，
# 跑一下这个脚本，就知道有没有把之前跑通的东西改坏——这正是 Linux 内核
# 持续集成那套逻辑的最小化版本：不是靠审查相信代码是对的，是靠反复跑验证它是对的。
#
# 用法：先 `make` 编译出 build/os-image.bin，再 `./test-boot.sh`
#       换机型：MACHINE=q35 ./test-boot.sh（默认 pc）
#
# v0.7 起：通过 QEMU monitor 的 sendkey 模拟按键，验证键盘驱动；
#          内核打出整行后脚本自己发 quit，所以不再需要 timeout 命令
#
# ── 跨平台说明 ─────────────────────────────────────────────
# 原版假设在 Linux/Kali 上跑（qemu-system-i386）。macOS 上没有，所以做了自动适配：
#   * QEMU：优先用 PATH 里的 qemu-system-i386；找不到就用工具链里的启动器
#           （~/Documents/Harness WorkSpace/c-dev-apps/toolchain/qemu-i386，
#            它把 UTM 内置的 QEMU 封装成了命令行程序）
set -u

IMAGE="${1:-build/os-image.bin}"
LOG="boot-test.log"
TIMEOUT=8   # 秒。每一步（等启动、等整行输出）最多等这么久

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
echo "启动 QEMU（无图形界面），等内核就绪后用 monitor 的 sendkey 模拟按键..."
rm -f "$LOG"

# 等串口日志里出现某一行，最多等 TIMEOUT 秒
wait_for() {
    local n=0
    while [ "$n" -lt $((TIMEOUT * 10)) ]; do
        grep -qF "$1" "$LOG" 2>/dev/null && return 0
        sleep 0.1
        n=$((n + 1))
    done
    return 1
}

MACHINE="${MACHINE:-pc}"

# 往 QEMU monitor 喂命令：先等内核就绪，再逐个按键，等内核打出整行，最后 quit。
# 无论哪一步超时，最后都会发 quit，所以 QEMU 一定会退出，不需要外部的 timeout / kill。
feed_monitor() {
    wait_for "waiting for keyboard input"
    for k in "${KEYS[@]}"; do
        echo "sendkey $k"
        sleep 0.05
    done
    wait_for "zeroOS input:"
    echo "quit"
}

# 要敲的键：Hello, zeroOS! 中间故意多敲一个 x 再退格删掉
#   shift-h → H（Shift）   comma → ,   spc → 空格   shift-1 → !（Shift + 数字）
#   caps_lock o k caps_lock → OK（Caps Lock 开、关）
KEYS=(shift-h e l l o comma spc z e r x backspace o shift-o shift-s shift-1 spc caps_lock o k caps_lock ret)
EXPECTED_INPUT="zeroOS input: Hello, zeroOS! OK"

feed_monitor | "$QEMU" "${EXTRA_ARGS[@]}" -M "$MACHINE" -m 32 \
    -drive format=raw,file="$IMAGE" \
    -serial file:"$LOG" \
    -display none \
    -monitor stdio >/dev/null 2>&1

echo "---- 串口输出 ----"
cat "$LOG" 2>/dev/null || echo "(没有任何输出)"
echo "-------------------"

# 期待看到的几行关键日志，对应 v0.7 kmain() 里实际打印的内容——
# 以后内核行为变了，这里要跟着改，别让测试脚本和代码脱节
EXPECTED_LINES=(
    "entered protected mode, kmain() started"
    "IDT installed, CPU exceptions now caught"
    "PIT at 100 Hz, IRQ0 enabled"
    "PS/2 keyboard on IRQ1 enabled"
    "waiting for keyboard input"
    "$EXPECTED_INPUT ("
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

# 时钟中断：整行末尾的 ticks=N 必须大于 0，否则说明 IRQ0 没在跑
TICKS=$(grep -F "$EXPECTED_INPUT (" "$LOG" 2>/dev/null | sed -n 's/.*ticks=\([0-9]*\)).*/\1/p' | head -1)
if [ -n "$TICKS" ] && [ "$TICKS" -gt 0 ]; then
    echo "[通过] 时钟在走: ticks=$TICKS"
else
    echo "[缺失] 时钟没在走: ticks=${TICKS:-（没读到）}"
    PASS=0
fi

echo ""
if [ "$PASS" -eq 1 ]; then
    echo "结果：全部通过"
    exit 0
else
    echo "结果：有缺失。串口日志停在哪一行，问题大概率就在那一行之后的代码——"
    echo "      对照 boot.asm / kernel.c 里对应位置的注释定位"
    exit 1
fi
