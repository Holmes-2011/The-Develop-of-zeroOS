#include "floattest.h"
#include "kmath.h"
#include "console.h"
#include "cpu.h"
#include "pit.h"
#include "heap.h"

/* 语言模型生成一个词，绝大部分时间花在"矩阵 × 向量"上：每个参数做一次乘加。
   所以测出每秒能做多少次乘加，就能估算：每个词的时间 ≈ 参数个数 ÷ 每秒乘加次数。

   三种权重格式（都是每 32 个权重一组、共用一个缩放系数，和常见的量化格式同一思路）：
     f32  每个权重 4 字节，不量化
     q8   每个权重 1 字节（-127 ~ 127）
     q4   每个权重半字节（-8 ~ 7），两个权重挤在一个字节里 */

#define DIM   1024          /* 测试矩阵 1024 × 1024，约等于一个小模型里一层的大小 */
#define BLOCK 32

static volatile float sink;   /* 把结果写到这里，免得编译器觉得"算了也没人用"而整个删掉 */

static float dot_f32(const float *w, const float *x) {
    float s = 0;
    int i;
    for (i = 0; i < DIM; i++) s += w[i] * x[i];
    return s;
}

static float dot_q8(const signed char *w, const float *scale, const float *x) {
    float s = 0;
    int b, i;
    for (b = 0; b < DIM / BLOCK; b++) {
        float part = 0;
        for (i = 0; i < BLOCK; i++) part += (float) w[b * BLOCK + i] * x[b * BLOCK + i];
        s += part * scale[b];
    }
    return s;
}

static float dot_q4(const unsigned char *w, const float *scale, const float *x) {
    float s = 0;
    int b, i;
    for (b = 0; b < DIM / BLOCK; b++) {
        float part = 0;
        for (i = 0; i < BLOCK / 2; i++) {
            unsigned char v = w[b * BLOCK / 2 + i];
            part += (float) ((int) (v & 0xF) - 8) * x[b * BLOCK + 2 * i]
                  + (float) ((int) (v >> 4) - 8) * x[b * BLOCK + 2 * i + 1];
        }
        s += part * scale[b];
    }
    return s;
}

/* 反复做整个矩阵 × 向量，至少跑 1 秒（100 个时钟中断），返回每秒乘加次数（百万） */
static float measure(int kind, const void *w, const float *scale, const float *x) {
    unsigned int start, elapsed, rounds = 0;
    int r;
    float acc = 0;
    start = pit_ticks();
    while (start == pit_ticks()) { }            /* 对齐到一个时钟中断的边上，计时更准 */
    start = pit_ticks();
    do {
        for (r = 0; r < DIM; r++) {
            if (kind == 0)      acc += dot_f32((const float *) w + r * DIM, x);
            else if (kind == 1) acc += dot_q8((const signed char *) w + r * DIM, scale + r * (DIM / BLOCK), x);
            else                acc += dot_q4((const unsigned char *) w + r * DIM / 2, scale + r * (DIM / BLOCK), x);
        }
        rounds++;
        elapsed = pit_ticks() - start;
    } while (elapsed < 100);
    sink = acc;
    return (float) rounds * DIM * DIM / ((float) elapsed / 100.0f) / 1e6f;
}

static void report_estimate(const char *what, float mmacs) {
    float s1 = 1e9f / (mmacs * 1e6f), s3 = 3e9f / (mmacs * 1e6f);
    console_write("    ");
    console_write(what);
    console_write(": 1B model ~");
    console_write_float(s1, 1);
    console_write(" s/token, 3B model ~");
    console_write_float(s3, 1);
    console_write(" s/token\n");
}

void bench_run(void) {
    float *wf, *x, *scale, r_f32, r_q8, r_q4;
    signed char *w8;
    unsigned char *w4;
    unsigned int i;

    if (!cpu_get_info()->float_enabled) {
        console_write("floating point is not enabled on this CPU\n");
        return;
    }
    wf = kmalloc(DIM * DIM * 4);
    w8 = kmalloc(DIM * DIM);
    w4 = kmalloc(DIM * DIM / 2);
    scale = kmalloc(DIM * (DIM / BLOCK) * 4);
    x = kmalloc(DIM * 4);
    if (!wf || !w8 || !w4 || !scale || !x) {
        console_write("bench: not enough memory (needs about 6 MB)\n");
        kfree(wf); kfree(w8); kfree(w4); kfree(scale); kfree(x);
        return;
    }

    /* 填一些不全是 0 的数据（具体是什么数无所谓，速度和数值无关） */
    for (i = 0; i < DIM * DIM; i++) {
        wf[i] = (float) ((int) (i * 2654435761U >> 24) - 128) / 128.0f;
        w8[i] = (signed char) ((int) (i * 2654435761U >> 24) - 128);
    }
    for (i = 0; i < DIM * DIM / 2; i++) w4[i] = (unsigned char) (i * 2654435761U >> 24);
    for (i = 0; i < DIM * (DIM / BLOCK); i++) scale[i] = 0.01f;
    for (i = 0; i < DIM; i++) x[i] = (float) (i % 17) / 17.0f - 0.5f;

    console_write("matrix x vector, 1024 x 1024, about 1 second each...\n");
    r_f32 = measure(0, wf, scale, x);
    console_write("  f32: "); console_write_float(r_f32, 1); console_write(" M multiply-adds/s\n");
    r_q8 = measure(1, w8, scale, x);
    console_write("  q8 : "); console_write_float(r_q8, 1); console_write(" M multiply-adds/s\n");
    r_q4 = measure(2, w4, scale, x);
    console_write("  q4 : "); console_write_float(r_q4, 1); console_write(" M multiply-adds/s\n");

    console_write("estimated time per generated token (matrix work only):\n");
    report_estimate("8-bit", r_q8);
    report_estimate("4-bit", r_q4);
    console_write("note: single core; under QEMU on a non-x86 Mac every x86 instruction is\n"
                  "      emulated in software, so real x86 hardware is many times faster.\n");

    kfree(wf); kfree(w8); kfree(w4); kfree(scale); kfree(x);
}
