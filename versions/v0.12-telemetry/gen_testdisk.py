#!/usr/bin/env python3
# gen_testdisk.py —— 生成测试硬盘：每个扇区的内容都能由扇区号算出来，zeroOS 的 diskcheck 逐字节校验
#
# 用法：python3 gen_testdisk.py <输出文件> <大小MB> [额外写入的扇区号 ...]
#   大小 MB 以内的每个扇区都写上图案；额外的扇区号（可以远远超过大小）单独写上图案，
#   文件会被扩展成稀疏文件——用来在不占多少空间的情况下测 128GB 以外的 48 位寻址
#
# 扇区 L 的内容（512 字节）：
#   0  ~ 7   "zeroOSdk"
#   8  ~ 15  L（64 位，小端）
#   16 ~ 511 124 个 32 位数：xorshift32 序列，种子由 L 算出（和 disktest.c 里的 fill_pattern 一致）
import struct, sys

def sector(lba):
    lo, hi = lba & 0xFFFFFFFF, lba >> 32
    x = ((lo ^ ((hi * 0x9E3779B9) & 0xFFFFFFFF)) * 2654435761 + 1) & 0xFFFFFFFF
    if x == 0:
        x = 1
    words = []
    for _ in range(124):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        words.append(x)
    return b"zeroOSdk" + struct.pack("<Q", lba) + struct.pack("<124I", *words)

out, mb = sys.argv[1], int(sys.argv[2])
extra = [int(a) for a in sys.argv[3:]]
with open(out, "wb") as f:
    for lba in range(mb * 2048):
        f.write(sector(lba))
    for lba in extra:
        f.seek(lba * 512)
        f.write(sector(lba))
    # 把文件扩展到最后一个额外扇区之后再多 1MB，稀疏部分不占磁盘空间
    end = max([mb * 2048] + [l + 2048 for l in extra]) * 512
    f.truncate(end)
