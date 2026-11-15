#!/usr/bin/env python3
# elf_to_bin.py —— 把 kernel.elf 转成纯二进制（等价于 objcopy -O binary）
#
# 用法：python3 elf_to_bin.py [--mem-limit 0x400000] kernel.elf kernel.bin
#
# v0.15 起内核是高半内核：段的虚拟地址（p_vaddr）在 0xC0100000 起，加载地址（p_paddr）在 0x100000 起。
# 纯二进制按加载地址排布；--mem-limit 检查的是物理末端（含 .bss，它不占文件空间）。
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
vbase = None
for i in range(phnum):
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from('<IIIIIIII', data, phoff + i * phentsize)
    if p_type != 1: continue                     # 只看 PT_LOAD
    mem_end = max(mem_end, p_paddr + p_memsz)
    if vbase is None: vbase = p_vaddr - p_paddr
    if p_filesz: segs.append((p_paddr, p_filesz, p_offset))
if not segs: raise SystemExit('no PT_LOAD segments')
if limit is not None and mem_end > limit:
    raise SystemExit('错误：内核在物理内存里一直延伸到 0x%X（含 .bss），超过了 0x%X\n'
                     '      这是给自己定的 4MB 预算（见 Makefile），内核真到这么大该先想想是不是有东西不该放进内核' % (mem_end, limit))
base = min(x[0] for x in segs)
end = max(p + size for p, size, _ in segs)
out = bytearray(end - base)
for p, size, off in segs:
    out[p-base:p-base+size] = data[off:off+size]
open(dst, 'wb').write(out)
if limit is not None:
    print('  物理占用 0x%X ~ 0x%X（含 .bss），上限 0x%X；虚拟地址 = 物理 + 0x%X  OK' % (base, mem_end, limit, vbase or 0))
