#!/bin/bash
# test-boot.sh —— 自动化引导测试，而不是人工盯着 QEMU 窗口看
#
# 这是把"审查代码"升级成"可重复测试"的第一步：以后每次改完代码，
# 跑一下这个脚本，就知道有没有把之前跑通的东西改坏——这正是 Linux 内核
# 持续集成那套逻辑的最小化版本：不是靠审查相信代码是对的，是靠反复跑验证它是对的。
#
# 用法：先 `make` 编译出 os-image.bin，再 `./test-boot.sh`

set -e

IMAGE="os-image.bin"
LOG="boot-test.log"
TIMEOUT=5   # 秒。正常情况下内核几乎瞬间就跑到 halt 循环了，5秒绰绰有余

if [ ! -f "$IMAGE" ]; then
    echo "错误：找不到 $IMAGE，先运行 make"
    exit 1
fi

echo "启动 QEMU（无图形界面，超时 ${TIMEOUT}s 自动终止）..."
rm -f "$LOG"
timeout "$TIMEOUT" qemu-system-i386 \
    -drive format=raw,file="$IMAGE" \
    -serial file:"$LOG" \
    -display none \
    || true   # timeout 杀掉 qemu 会返回非0，这是预期行为，不算脚本失败

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
