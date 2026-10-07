#!/usr/bin/env python3
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
if data[:4] != b'\x7fELF' or data[5] != 1 or data[4] != 1:
    raise SystemExit('expected little-endian ELF32')
phoff = struct.unpack_from('<I', data, 28)[0]
phentsize = struct.unpack_from('<H', data, 42)[0]
phnum = struct.unpack_from('<H', data, 44)[0]
segs = []
for i in range(phnum):
    p = struct.unpack_from('<IIIIIIII', data, phoff + i * phentsize)
    if p[0] == 1 and p[4]: segs.append((p[2], p[4], p[1]))
if not segs: raise SystemExit('no PT_LOAD segments')
base = min(x[0] for x in segs)
end = max(v + size for v, size, _ in segs)
out = bytearray(end - base)
for v, size, off in segs:
    # This linker script places the first load address at 0x1000 while the
    # ELF header occupies file offset 0; omit that non-loadable prefix.
    source = v if off == 0 and v >= 0x1000 else off
    out[v-base:v-base+size] = data[source:source+size]
open(dst, 'wb').write(out)
