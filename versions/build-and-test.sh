#!/bin/bash
# build-and-test.sh —— 把某个版本目录里的源码重新构建、启动、抓串口日志
#
# 用途：版本档案里的"测试结果"不该只是文字描述，应该有可复现的原始输出。
#       这个脚本用版本目录里的源码 + 共享的 boot.asm/entry.asm/link.ld
#       重新构建一次，启动 QEMU，把串口输出存成该目录下的 serial-output.txt。
#
# 用法：
#   ./build-and-test.sh v0.3-idt
#   ./build-and-test.sh v0.5-pic
#
# 设计说明：
#   * 源码可以在版本目录里，也可以在它的 upstream/ 子目录里（两种都支持）
#   * 只编译该版本实际拥有的文件——v0.1/v0.2 没有 idt.c/pic.c 就不编
#   * boot.asm/entry.asm/link.ld 优先用版本目录里的，没有就用工程根目录的
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"      # versions/
PROJ="$(dirname "$HERE")"                  # 工程根目录
VER="${1:-}"
[ -n "$VER" ] || { echo "用法: $(basename "$0") <版本目录名>（如 v0.3-idt）" >&2; exit 1; }

VDIR="$HERE/$VER"
[ -d "$VDIR" ] || { echo "找不到版本目录: $VDIR" >&2; exit 1; }

CDEV="${CDEV:-$HOME/Documents/Harness WorkSpace/c-dev-apps}"
ZIG="$CDEV/toolchain/zig/zig"
NASM="$CDEV/toolchain/nasm/nasm"
QEMU="$CDEV/toolchain/qemu-i386"
BIOS="/Applications/UTM.app/Contents/Resources/qemu"

# 搜索结果分两类，这一点很关键：
#
#   共享文件（boot.asm / entry.asm / link.ld）
#       版本目录 → upstream/ → versions/v0.5.1/
#       这三样从 v0.3 起就一直没随包发过，只在当前版本里有一份，可以共用
#
#   版本专属模块（kernel.c / idt.c / pic.c / isr.asm）
#       **只在版本目录和它的 upstream/ 里找，绝不回落到工程根目录**
#       否则 v0.2 会悄悄混进当前版本的 isr.asm/idt.c/pic.c，
#       编出一个"四不像"的镜像，测试结果就全是错的
SHARED_DIRS=("$VDIR" "$VDIR/upstream" "$HERE/v0.5.1")
MODULE_DIRS=("$VDIR" "$VDIR/upstream")

find_in() {   # find_in <文件名> <目录...>  → 回显绝对路径
  local n="$1"; shift
  local d
  for d in "$@"; do
    [ -f "$d/$n" ] && { echo "$d/$n"; return 0; }
  done
  return 1
}
find_shared() { find_in "$1" "${SHARED_DIRS[@]}"; }
find_module() { find_in "$1" "${MODULE_DIRS[@]}"; }

W="$(mktemp -d "${TMPDIR:-/tmp}/zeroos-build.XXXXXX")"
trap 'rm -rf "$W"' EXIT

CFLAGS="--target=i386-unknown-none -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -O2 -Wall -Wextra"
INCS=()
for d in "${SHARED_DIRS[@]}"; do [ -d "$d" ] && INCS+=("-I$d"); done

echo "═══ 构建 $VER ═══"
OBJS=()

# 1) 引导扇区
BOOT_ASM="$(find_shared boot.asm)" || { echo "  找不到 boot.asm"; exit 1; }
"$NASM" -f bin "$BOOT_ASM" -o "$W/boot.bin" 2>/dev/null || { echo "  引导扇区汇编失败"; exit 1; }
BSZ=$(stat -f%z "$W/boot.bin")
[ "$BSZ" = 512 ] || { echo "  引导扇区是 $BSZ 字节，应为 512"; exit 1; }
printf "  引导扇区      %s（来自 %s）\n" "512 字节 ✓" "${BOOT_ASM#$PROJ/}"

# 2) 入口垫片（名字可能叫 entry.asm 或 kernel_entry.asm）
ENTRY="$(find_shared entry.asm)" || ENTRY="$(find_shared kernel_entry.asm)" || { echo "  找不到入口垫片"; exit 1; }
"$NASM" -f elf32 "$ENTRY" -o "$W/entry.o" 2>/dev/null || { echo "  入口垫片汇编失败"; exit 1; }
OBJS+=("$W/entry.o")
printf "  入口垫片      ✓（来自 %s）\n" "${ENTRY#$PROJ/}"

# 3) 各汇编模块
for m in isr; do
  if S="$(find_module $m.asm)"; then
    "$NASM" -f elf32 "$S" -o "$W/$m.o" 2>/dev/null || { echo "  $m.asm 汇编失败"; exit 1; }
    OBJS+=("$W/$m.o"); printf "  %-13s ✓（来自 %s）\n" "$m.asm" "${S#$PROJ/}"
  fi
done

# 4) 各 C 模块
for m in kernel idt pic; do
  if S="$(find_module $m.c)"; then
    clang $CFLAGS "${INCS[@]}" -c "$S" -o "$W/$m.o" 2>"$W/$m.err" \
      || { echo "  $m.c 编译失败："; head -5 "$W/$m.err" | sed 's/^/      /'; exit 1; }
    OBJS+=("$W/$m.o"); printf "  %-13s ✓（来自 %s）\n" "$m.c" "${S#$PROJ/}"
  fi
done

# 5) 链接
LINKER="$(find_shared link.ld)" || { echo "  找不到 link.ld"; exit 1; }
"$ZIG" cc --target=x86-freestanding -nostdlib -fno-sanitize=all \
  -Wl,-T,"$LINKER" -Wl,--build-id=none -o "$W/kernel.elf" "${OBJS[@]}" 2>"$W/link.err" \
  || { echo "  链接失败："; head -8 "$W/link.err" | sed 's/^/      /'; exit 1; }

# 6) ELF → 纯二进制 + 补齐到 20KB + 整体补零到 512KB
"$ZIG" objcopy -O binary "$W/kernel.elf" "$W/kernel.bin" 2>/dev/null
KSZ=$(stat -f%z "$W/kernel.bin")
LIMIT=$((40 * 512))
[ "$KSZ" -le "$LIMIT" ] || { echo "  内核 $KSZ 字节超过 40 扇区（$LIMIT）"; exit 1; }
dd if=/dev/zero bs=512 count=$(( (LIMIT - KSZ + 511) / 512 )) >> "$W/kernel.bin" 2>/dev/null
cat "$W/boot.bin" "$W/kernel.bin" > "$W/os-image.bin"
ISZ=$(stat -f%z "$W/os-image.bin")
dd if=/dev/zero bs=512 count=$(( (524288 - ISZ + 511) / 512 )) >> "$W/os-image.bin" 2>/dev/null
printf "  内核 %s 字节 → 镜像 %s 字节（含补零）\n" "$KSZ" "$(stat -f%z "$W/os-image.bin")"

# 7) 启动并抓串口
echo "═══ 启动（机型 pc）═══"
( cd "$W" && "$QEMU" -L "$BIOS" -M pc -m 32 \
    -drive "file=$W/os-image.bin,format=raw,index=0,media=disk" -boot c \
    -display none -serial "file:$W/serial.txt" -monitor none >/dev/null 2>&1 & echo $! > "$W/pid" )
sleep 2
kill "$(cat "$W/pid" 2>/dev/null)" 2>/dev/null
sleep 0.3

OUT="$VDIR/serial-output.txt"
{
  echo "# 由 build-and-test.sh 生成：$VER"
  echo "# 生成时间：$(date '+%Y-%m-%d %H:%M:%S')"
  echo "# 命令：./build-and-test.sh $VER"
  echo "# 环境：QEMU 10.0.2 / -M pc (i440FX) / 32MB / 镜像 $(( $(stat -f%z "$W/os-image.bin") )) 字节"
  echo "# ────────────────────────────────────────────────"
  cat "$W/serial.txt" 2>/dev/null || echo "(没有任何串口输出)"
} > "$OUT"

echo "═══ 串口输出 ═══"
sed 's/^/  | /' "$W/serial.txt" 2>/dev/null || echo "  (无输出)"
echo "═══ 已保存到 ${OUT#$PROJ/} ═══"
