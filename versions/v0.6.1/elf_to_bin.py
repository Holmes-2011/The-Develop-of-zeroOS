#!/usr/bin/env python3
# elf_to_bin.py —— 把 kernel.elf 转成纯二进制（等价于 objcopy -O binary）
#
# 用法：python3 elf_to_bin.py [--mem-limit 0x7C00] kernel.elf kernel.bin
#
# --mem-limit：检查内核在内存里的末端（含 .bss —— 它不占文件空间，
#              objcopy 不会写出它，只看文件大小查不出越界）不超过这个地址。
import struct, sys
args = sys.argv[1:]
limit = None
if args[:1] == ['--mem-limit']:
    limit = int(args[1], 0)
    args = args[2:]
src, dst = args
data = open(src, 'rb').read()
if data[:4] != b'\x7fELF' or data[5] != 1 or data[4] != 1:
    raise SystemExit('expected little-endian ELF32')
phoff = struct.unpack_from('<I', data, 28)[0]
phentsize = struct.unpack_from('<H', data, 42)[0]
phnum = struct.unpack_from('<H', data, 44)[0]
segs = []
mem_end = 0
for i in range(phnum):
    p = struct.unpack_from('<IIIIIIII', data, phoff + i * phentsize)
    if p[0] != 1: continue                       # 只看 PT_LOAD
    mem_end = max(mem_end, p[2] + p[5])          # vaddr + memsz
    if p[4]: segs.append((p[2], p[4], p[1]))     # 有文件内容的段才写出
if not segs: raise SystemExit('no PT_LOAD segments')
if limit is not None and mem_end > limit:
    raise SystemExit('错误：内核在内存里一直延伸到 0x%X（含 .bss），超过了 0x%X\n'
                     '      再往上就会盖掉引导扇区里的 GDT' % (mem_end, limit))
base = min(x[0] for x in segs)
end = max(v + size for v, size, _ in segs)
out = bytearray(end - base)
for v, size, off in segs:
    out[v-base:v-base+size] = data[off:off+size]
open(dst, 'wb').write(out)
if limit is not None:
    print('  内存占用 0x%X ~ 0x%X（含 .bss），上限 0x%X  OK' % (base, mem_end, limit))
