#include "kmath.h"
#include "console.h"

/* float 和它的 32 位二进制表示互相转换（符号 1 位 | 指数 8 位 | 尾数 23 位） */
static unsigned int f2u(float f) { union { float f; unsigned int u; } v; v.f = f; return v.u; }
static float u2f(unsigned int u) { union { float f; unsigned int u; } v; v.u = u; return v.f; }

int km_isnan(float x) { return (f2u(x) & 0x7FFFFFFF) > 0x7F800000; }
int km_isinf(float x) { return (f2u(x) & 0x7FFFFFFF) == 0x7F800000; }

float fabsf(float x) { return u2f(f2u(x) & 0x7FFFFFFF); }

float sqrtf(float x) {
    /* 直接用 SSE 的开方指令，硬件保证结果是正确舍入的 */
    __asm__("sqrtss %1, %0" : "=x"(x) : "x"(x));
    return x;
}

float floorf(float x) {
    float t;
    if (km_isnan(x) || fabsf(x) >= 8388608.0f) return x;   /* 2^23 以上的 float 本来就是整数 */
    t = (float) (int) x;                                   /* 向零取整 */
    return t > x ? t - 1.0f : t;
}

/* 2^n：直接拼出指数位 */
static float pow2i(int n) {
    if (n < -126) return 0.0f;
    if (n > 127) return KM_INF;
    return u2f((unsigned int) (n + 127) << 23);
}

/* e^x = 2^n · e^r，其中 n = round(x / ln2)，r = x - n·ln2，|r| ≤ ln2/2 ≈ 0.347
   ln2 拆成高低两部分（Cody-Waite），减法时不丢精度；e^r 用 7 阶泰勒多项式 */
float expf(float x) {
    const float LN2_HI = 0.693145751953125f, LN2_LO = 1.428606765330187e-06f;
    float r, p;
    int n;
    if (km_isnan(x)) return x;
    if (x > 88.72283f) return KM_INF;
    if (x < -103.97208f) return 0.0f;
    n = (int) floorf(x * 1.4426950408889634f + 0.5f);
    r = (x - (float) n * LN2_HI) - (float) n * LN2_LO;
    p = 1.0f + r * (1.0f + r * (0.5f + r * (1.0f / 6 + r * (1.0f / 24 + r * (1.0f / 120
          + r * (1.0f / 720 + r * (1.0f / 5040)))))));
    /* n 可能低到 -150：分两次乘，避免 2^n 本身下溢成 0 */
    if (n < -125) return p * pow2i(n + 64) * pow2i(-64);
    if (n > 127) return p * pow2i(n - 1) * 2.0f;
    return p * pow2i(n);
}

/* ln x = e·ln2 + ln m，其中 x = m · 2^e，m ∈ [√½, √2)
   ln m = 2·atanh(s)，s = (m-1)/(m+1)，|s| ≤ 0.172，用级数 2(s + s³/3 + s⁵/5 + …) */
float logf(float x) {
    unsigned int u = f2u(x);
    int e;
    float m, s, s2, t;
    if (km_isnan(x) || x < 0.0f) return KM_NAN;
    if (x == 0.0f) return -KM_INF;
    if (km_isinf(x)) return x;
    if (u < 0x00800000) {             /* 非规格化数：先放大 2^23 再算 */
        x *= 8388608.0f;
        u = f2u(x);
        e = -23;
    } else {
        e = 0;
    }
    e += (int) (u >> 23) - 127;
    m = u2f((u & 0x007FFFFF) | 0x3F800000);   /* 尾数，范围 [1, 2) */
    if (m > 1.41421356f) { m *= 0.5f; e++; }
    s = (m - 1.0f) / (m + 1.0f);
    s2 = s * s;
    t = s * (2.0f + s2 * (2.0f / 3 + s2 * (2.0f / 5 + s2 * (2.0f / 7 + s2 * (2.0f / 9)))));
    return (float) e * 0.693147180559945f + t;
}

float powf(float x, float y) {
    if (y == 0.0f) return 1.0f;
    if (x == 1.0f) return 1.0f;
    if (x == 0.0f) return y > 0.0f ? 0.0f : KM_INF;
    if (x < 0.0f) return KM_NAN;    /* 推理引擎只会用到正数的幂 */
    return expf(y * logf(x));
}

/* sin / cos：先把 x 减到 [-π/4, π/4]（减去 k·π/2，π/2 拆成三段保证精度），
   再按 k 落在哪个象限，选 sin 或 cos 的多项式、决定正负号 */
static float sin_poly(float r) {
    float r2 = r * r;
    return r + r * r2 * (-1.0f / 6 + r2 * (1.0f / 120 + r2 * (-1.0f / 5040 + r2 * (1.0f / 362880))));
}
static float cos_poly(float r) {
    float r2 = r * r;
    return 1.0f + r2 * (-0.5f + r2 * (1.0f / 24 + r2 * (-1.0f / 720 + r2 * (1.0f / 40320))));
}
static float reduce(float x, int *quadrant) {
    const float P1 = 1.5703125f, P2 = 4.837512969970703125e-4f, P3 = 7.54978995489188216e-8f;
    float k = floorf(x * 0.63661977236758134f + 0.5f);   /* 2/π */
    *quadrant = (int) k & 3;
    return ((x - k * P1) - k * P2) - k * P3;
}

float sinf(float x) {
    int q;
    float r;
    if (km_isnan(x) || km_isinf(x)) return KM_NAN;
    r = reduce(x, &q);
    switch (q) {
    case 0:  return sin_poly(r);
    case 1:  return cos_poly(r);
    case 2:  return -sin_poly(r);
    default: return -cos_poly(r);
    }
}

float cosf(float x) {
    int q;
    float r;
    if (km_isnan(x) || km_isinf(x)) return KM_NAN;
    r = reduce(x, &q);
    switch (q) {
    case 0:  return cos_poly(r);
    case 1:  return -sin_poly(r);
    case 2:  return -cos_poly(r);
    default: return sin_poly(r);
    }
}

float tanhf(float x) {
    float a = fabsf(x), t;
    if (km_isnan(x)) return x;
    if (a > 9.0f) return x > 0 ? 1.0f : -1.0f;         /* 这时 tanh 和 ±1 在 float 精度下已无区别 */
    if (a < 0.0004f) return x;                         /* 很小的时候 tanh(x) ≈ x，比下面的公式更准 */
    t = 1.0f - 2.0f / (expf(2.0f * a) + 1.0f);
    return x > 0 ? t : -t;
}

void console_write_float(float x, int decimals) {
    unsigned int whole, scale = 1, frac;
    int i;
    if (km_isnan(x)) { console_write("nan"); return; }
    if (x < 0) { console_putc('-'); x = -x; }
    if (km_isinf(x)) { console_write("inf"); return; }
    if (x >= 4294967040.0f) { console_write("(big)"); return; }
    for (i = 0; i < decimals; i++) scale *= 10;
    whole = (unsigned int) x;
    frac = (unsigned int) ((x - (float) whole) * (float) scale + 0.5f);
    if (frac >= scale) { whole++; frac -= scale; }    /* 0.9996 保留 3 位 → 1.000 */
    console_write_dec(whole);
    if (decimals > 0) {
        console_putc('.');
        for (scale /= 10; scale > 1 && frac < scale; scale /= 10) console_putc('0');   /* 补前导零 */
        console_write_dec(frac);
    }
}
