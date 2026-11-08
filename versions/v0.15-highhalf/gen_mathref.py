#!/usr/bin/env python3
# gen_mathref.py —— 生成 mathref.h：数学函数的参考值（用 Python 的双精度 math 库算）
# 用法：python3 gen_mathref.py > mathref.h
# mathtest 命令拿 zeroOS 自己算出的单精度结果和这里比，报告最大误差
import math, struct

def f32(x):   # 先把输入舍入成 float，参考值按"这个 float 输入"来算，比较才公平
    return struct.unpack('f', struct.pack('f', x))[0]

def table(name, fn, xs):
    rows = []
    for x in xs:
        x = f32(x)
        rows.append('    { %.9ef, %.17e },' % (x, fn(x)))
    return 'static const struct ref %s[] = {\n%s\n};\n' % (name, '\n'.join(rows))

lin = lambda a, b, n: [a + (b - a) * i / (n - 1) for i in range(n)]
print('/* 由 gen_mathref.py 生成，不要手改 */')
print('struct ref { float x; double y; };\n')
print(table('ref_exp',  math.exp,  lin(-87, 88, 61) + lin(-1, 1, 21)))
print(table('ref_log',  math.log,  [1e-30, 1e-10, 1e-5, 0.001, 0.1, 0.5, 0.9, 0.99, 0.999, 1.001, 1.01, 1.1,
                                    1.5, 2, 2.718281828, 3, 10, 100, 1234.5, 1e5, 1e10, 1e30, 3e38]))
print(table('ref_sin',  math.sin,  lin(-10, 10, 41) + [100, 1000, 4096, 12345.678]))
print(table('ref_cos',  math.cos,  lin(-10, 10, 41) + [100, 1000, 4096, 12345.678]))
print(table('ref_tanh', math.tanh, lin(-10, 10, 41) + [1e-5, -3e-4, 0.0005]))
print(table('ref_sqrt', math.sqrt, [0, 1e-30, 1e-8, 0.25, 0.5, 1, 2, 3, 10, 12345.6, 1e20, 3e38]))
# RoPE 里常见的形式：theta^(-2i/d)
pw = [(10000.0, -2 * i / 128) for i in range(0, 64, 4)] + [(2, 10), (2, -10), (0.5, 3.5), (7.5, 2.25), (1e4, 0.5)]
rows = ['    { %.9ef, %.9ef, %.17e },' % (f32(a), f32(b), f32(a) ** f32(b)) for a, b in pw]
print('struct ref2 { float x, y; double z; };\nstatic const struct ref2 ref_pow[] = {\n%s\n};' % '\n'.join(rows))
