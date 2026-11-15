#!/usr/bin/env python3
"""check_ppm.py <file.ppm> <x> <y> <r> <g> <b> ... —— 检查若干像素（纯标准库，CI 上也能跑）"""
import sys

data = open(sys.argv[1], 'rb').read()
assert data[:2] == b'P6', 'not a P6 ppm'
i = 2
vals = []
while len(vals) < 3:
    while data[i:i+1].isspace():
        i += 1
    if data[i:i+1] == b'#':
        while data[i:i+1] != b'\n':
            i += 1
        continue
    j = i
    while not data[j:j+1].isspace():
        j += 1
    vals.append(int(data[i:j]))
    i = j
while data[i:i+1].isspace():
    i += 1
w, h, maxv = vals
assert maxv == 255, 'only 8-bit ppm supported'

bad = 0
args = sys.argv[2:]
for k in range(0, len(args), 5):
    x, y, r, g, b = map(int, args[k:k+5])
    off = i + (y * w + x) * 3
    got = tuple(data[off:off+3])
    if got != (r, g, b):
        print(f'pixel ({x},{y}) = {got}, want {(r, g, b)}')
        bad += 1
print('ppm ok' if not bad else f'{bad} mismatches')
sys.exit(1 if bad else 0)
