#ifndef KMATH_H
#define KMATH_H

/* 单精度数学函数——推理引擎要用的那几个：
     sqrtf  归一化（RMSNorm / LayerNorm）
     expf   softmax、SiLU / GELU 激活
     logf / powf  RoPE 位置编码的频率
     sinf / cosf  RoPE 的旋转
     tanhf  GELU 的近似式
   这个文件用 SSE 编译（Makefile 里的 SIMD_SRCS），只能在主循环里调用，不能在中断里调用 */

#define KM_PI   3.14159265358979323846f
#define KM_INF  __builtin_inff()
#define KM_NAN  __builtin_nanf("")

float sqrtf(float x);
float fabsf(float x);
float floorf(float x);
float expf(float x);
float logf(float x);
float powf(float x, float y);
float sinf(float x);
float cosf(float x);
float tanhf(float x);
int   km_isnan(float x);
int   km_isinf(float x);

/* 打印一个浮点数（保留 decimals 位小数）——console.c 是"核心"代码不能碰浮点，所以放在这里 */
void  console_write_float(float x, int decimals);

#endif
