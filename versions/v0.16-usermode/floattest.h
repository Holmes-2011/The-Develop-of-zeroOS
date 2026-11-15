#ifndef FLOATTEST_H
#define FLOATTEST_H

/* mathtest 命令：浮点 / SSE 是否真的开启、数学函数的精度、特殊值、
   中断会不会破坏 XMM 寄存器。逐项打印 [ok] / [FAIL]，返回失败项数 */
int mathtest_run(void);

/* bench 命令：实测矩阵 × 向量的速度（float、8 位量化、4 位量化），
   并据此估算 1B / 3B 参数的模型每生成一个词要多久 */
void bench_run(void);

#endif
