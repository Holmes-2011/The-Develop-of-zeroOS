#!/bin/bash
# soak.sh —— 模拟用户 10 轮混合使用（"过筛子"用），不是功能测试，是找崩溃 / 泄漏 / 账目不平。
#
# 用法：./soak.sh [pc|q35]      （先 make；每轮约 18 秒，共约 3 分钟）
# 每轮：查询类工具、自检、确认框（乱按、按 n、按 y）、开发者模式开关、故障注入、memtest、get_memory。
# 结束后打印统计：异常 / FAIL / memtest 通过次数 / 一致性检查 / 确认框次数 / 审计条数 / 空闲页（应回到初值）。
set -u
M="${1:-pc}"
HERE="$(cd "$(dirname "$0")" && pwd)"
IMG="$HERE/build/os-image.bin"
[ -f "$IMG" ] || { echo "先 make"; exit 1; }
CDEV="${CDEV:-$HOME/Documents/Harness WorkSpace/c-dev-apps}"
if command -v qemu-system-i386 >/dev/null 2>&1; then QEMU=qemu-system-i386; EXTRA=(); else QEMU="$CDEV/toolchain/qemu-i386"; EXTRA=(-L /Applications/UTM.app/Contents/Resources/qemu); fi
L="$HERE/soak-$M.log"; : > "$L"

prompts(){ grep -o "zeroOS> " "$L" | wc -l | tr -d ' '; }
boxes(){ grep -c "CONFIRM" "$L"; }
wu(){ local n=0; while ! eval "$1" && [ $n -lt 300 ]; do sleep 0.1; n=$((n+1)); done; }
keys(){ local s="$1" i c; for ((i=0;i<${#s};i++)); do c="${s:$i:1}"; case "$c" in " ") echo "sendkey spc";; "_") echo "sendkey shift-minus";; *) echo "sendkey $c";; esac; sleep 0.02; done; echo "sendkey ret"; }
word(){ local b=$(prompts); keys "$1"; wu "[ \$(prompts) -gt $b ]"; }
ask(){ local b=$(prompts) x=$(boxes); keys "$1"; wu "[ \$(boxes) -gt $x ]"; sleep 0.2; for k in $2; do echo "sendkey $k"; sleep 0.1; done; wu "[ \$(prompts) -gt $b ]"; }
{
  wu "[ \$(prompts) -ge 1 ]"
  word "tool get_memory"
  for r in 1 2 3 4 5 6 7 8 9 10; do
    word "tool get_uptime"; word "tools"; word "tool get_cpu"; word "tool get_metrics"; word "tool list_disks"
    word "tool heap_check"; word "tool pmm_check"; word "tool stack_check"
    word "tool set_telstream on"; word "tool set_telstream off"
    ask "tool clear_events" "x q n"              # 乱按再按 n
    ask "tool clear_events" "y"
    word "tool inject_fault spin 300"            # 默认模式：拒绝
    word "tool inject_fault"                     # 参数缺失
    word "tool inject_fault spin"                # 不带数字
    ask "devmode on" "y"
    ask "tool inject_fault spin 300" "y"
    ask "tool inject_fault heap_leak" "n"
    ask "tool inject_fault demand_touch" "y"
    ask "tool inject_fault bad_free" "y"
    ask "tool inject_fault stack_use 8" "y"
    ask "tool inject_fault spin" "y"             # 开发者模式下参数错：确认后 bad_args
    word "devmode off"
    word "memtest"; word "tool heap_check"; word "tool pmm_check"
    word "echo round $r"; word "tool get_memory"
  done
  word "audit"; word "tool get_events"; word "events"; echo quit
} | "$QEMU" "${EXTRA[@]}" -M "$M" -m 64 -drive format=raw,file="$IMG",index=0 -serial file:"$L" -display none -monitor stdio >/dev/null 2>&1

echo "soak($M): 异常 $(grep -c 'CPU exception ->' "$L")，FAIL $(grep -c '\[FAIL\]' "$L")，memtest 全过 $(grep -c "memtest: $(grep -oE 'memtest: [0-9]+/' "$L" | head -1 | grep -oE '[0-9]+')/$(grep -oE 'memtest: [0-9]+/[0-9]+' "$L" | head -1 | grep -oE '/[0-9]+' | tr -d /) passed" "$L")/10，heap_check ok $(grep -c '^heap_check -> ok' "$L")，pmm_check ok $(grep -c '^pmm_check -> ok' "$L")，确认框 $(grep -c '+-- CONFIRM' "$L")，$(grep -o 'audit: [0-9]* calls' "$L")，空闲页: $(grep -oE 'free_pages=[0-9]+' "$L" | sort -u | tr '\n' ' ')"
[ "$(grep -c 'CPU exception ->' "$L")" -eq 0 ] && [ "$(grep -c '\[FAIL\]' "$L")" -eq 0 ]
