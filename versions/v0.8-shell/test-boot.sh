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
# v0.8 起：敲一串命令测命令行；再用 monitor 的 pmemsave 把显存（0xB8000）存下来，
#          直接核对屏幕上的文字（包括写满一屏后的上滚、clear 清屏）
#          换内存大小：MEM=128 ./test-boot.sh（默认 32，用来核对 mem 命令报的大小）
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
MACHINE="${MACHINE:-pc}"
MEM="${MEM:-32}"
echo "启动 QEMU（-M $MACHINE，${MEM} MB），用 monitor 的 sendkey 敲命令..."
rm -f "$LOG" screen-1.bin screen-2.bin

# 串口日志里 "zeroOS> " 提示符出现了几次 = 命令行已经处理完几行
prompts() { grep -o "zeroOS> " "$LOG" 2>/dev/null | wc -l | tr -d ' '; }

# 等某个条件成立，最多等 TIMEOUT 秒
wait_until() {
    local n=0
    while [ "$n" -lt $((TIMEOUT * 10)) ]; do
        eval "$1" && return 0
        sleep 0.1
        n=$((n + 1))
    done
    return 1
}

# 敲一行命令（一串 sendkey），然后等命令行打出下一个提示符
type_line() {
    local before k
    before=$(prompts)
    for k in "$@"; do
        echo "sendkey $k"
        sleep 0.03
    done
    echo "sendkey ret"
    wait_until '[ "$(prompts)" -gt "$before" ]'
}

# 往 QEMU monitor 喂命令。无论哪一步超时，最后都会发 quit，QEMU 一定会退出
feed_monitor() {
    wait_until '[ "$(prompts)" -ge 1 ]'
    type_line h e l p
    type_line u p t i m e
    type_line m e m
    # echo Hi, zeroOS! —— 中间故意多敲一个 x 再退格删掉；覆盖 Shift、符号、空格、退格
    type_line e c h o spc shift-h i comma spc z e r x backspace o shift-o shift-s shift-1
    type_line f o o
    type_line spc spc                     # 只有空格的行：什么都不做
    type_line h e l p                     # 再来一次，让内容超过 25 行，触发上滚
    echo "pmemsave 0xb8000 4000 screen-1.bin"
    sleep 0.3
    type_line c l e a r
    echo "pmemsave 0xb8000 4000 screen-2.bin"
    sleep 0.3
    echo "quit"
}

feed_monitor | "$QEMU" "${EXTRA_ARGS[@]}" -M "$MACHINE" -m "$MEM" \
    -drive format=raw,file="$IMAGE" \
    -serial file:"$LOG" \
    -display none \
    -monitor stdio >/dev/null 2>&1

echo "---- 串口输出 ----"
cat "$LOG" 2>/dev/null || echo "(没有任何输出)"
echo "-------------------"

PASS=1
check() {   # check <说明> <命令>：命令成功算通过
    if eval "$2" >/dev/null 2>&1; then
        echo "[通过] $1"
    else
        echo "[失败] $1"
        PASS=0
    fi
}
in_log() { grep -qF -- "$1" "$LOG"; }

# 把显存（每个字符 2 字节：字符 + 颜色）还原成 25 行文字
screen_text() {
    python3 -c '
import sys
d = open(sys.argv[1], "rb").read()
for r in range(25):
    print(bytes(d[(r*80+c)*2] for c in range(80)).decode("ascii", "replace").rstrip())
' "$1"
}

# ---- 串口：启动日志 ----
for line in "entered protected mode, kmain() started" \
            "IDT installed, CPU exceptions now caught" \
            "PIT at 100 Hz, IRQ0 enabled" \
            "PS/2 keyboard on IRQ1 enabled" \
            "shell ready on screen and COM1"; do
    check "启动日志: $line" "in_log '$line'"
done

# ---- 串口：各个命令 ----
for c in help clear echo uptime mem version; do
    check "help 列出了 $c" "grep -qE '^  $c +[a-z]' '$LOG'"
done
TICKS=$(sed -n 's/^uptime: [0-9]*\.[0-9]* s (\([0-9]*\) ticks)$/\1/p' "$LOG" | head -1)
check "uptime 格式正确且时钟在走（ticks=${TICKS:-?}）" '[ -n "$TICKS" ] && [ "$TICKS" -gt 0 ]'
check "mem 报的内存 = $MEM MB" "in_log 'RAM:    $((MEM * 1024)) KB ($MEM MB, from CMOS)'"
check "mem 报了内核范围" "in_log 'kernel: 0x00001000 - 0x0000'"
check "echo + Shift/符号/退格: Hi, zeroOS!" "grep -qx 'Hi, zeroOS!' '$LOG'"
check "未知命令报错" "in_log \"unknown command: foo (type 'help')\""

# ---- 屏幕（显存）----
if [ -f screen-1.bin ] && [ -f screen-2.bin ]; then
    screen_text screen-1.bin > screen-1.txt
    screen_text screen-2.bin > screen-2.txt
    echo "---- 屏幕（clear 之前）----"
    cat screen-1.txt
    echo "---------------------------"
    check "屏幕上有命令输出 Hi, zeroOS!" "grep -qx 'Hi, zeroOS!' screen-1.txt"
    check "屏幕上有未知命令提示" "grep -qF 'unknown command: foo' screen-1.txt"
    check "写满一屏后上滚：最后一行是提示符" "[ \"\$(sed -n 25p screen-1.txt)\" = 'zeroOS>' ]"
    check "写满一屏后上滚：欢迎语已滚出屏幕" "! grep -qF 'zeroOS v0.8' screen-1.txt"
    check "clear 后第一行是提示符" "[ \"\$(sed -n 1p screen-2.txt)\" = 'zeroOS>' ]"
    check "clear 后其余 24 行都是空的" "[ -z \"\$(sed -n '2,25p' screen-2.txt | tr -d '[:space:]')\" ]"
else
    check "拿到了显存快照" "false"
fi
rm -f screen-1.bin screen-2.bin screen-1.txt screen-2.txt

echo ""
if [ "$PASS" -eq 1 ]; then
    echo "结果：全部通过"
    exit 0
else
    echo "结果：有缺失。串口日志停在哪一行，问题大概率就在那一行之后的代码——"
    echo "      对照 boot.asm / kernel.c 里对应位置的注释定位"
    exit 1
fi
