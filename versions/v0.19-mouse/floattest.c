#include "floattest.h"
#include "kmath.h"
#include "console.h"
#include "cpu.h"
#include "pit.h"
#include "mathref.h"

#define EPS 1.1920929e-7   /* float 的机器精度 2^-23：误差用"几个 EPS"来表示 */

static int passed, failed;

static void check(int ok, const char *what) {
    console_set_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED);
    console_write(ok ? "  [ok]   " : "  [FAIL] ");
    console_set_color(VGA_LIGHT_GREY);
    console_write(what);
    console_putc('\n');
    if (ok) passed++; else failed++;
}

static double dabs(double x) { return x < 0 ? -x : x; }

/* 误差 = |算出来 - 参考值| / max(|参考值|, floor)。
   floor = 1：值接近 0 时按绝对误差算（sin、cos、tanh、log 在 1 附近）；
   floor 很小：按相对误差算（exp、sqrt、pow） */
static double err_of(float got, double ref, double floor) {
    double scale = dabs(ref) > floor ? dabs(ref) : floor;
    if (km_isinf(got) && ref > 3.4e38) return 0;   /* 参考值超出 float 范围，算出 inf 是对的 */
    return dabs((double) got - ref) / scale;
}

static void check_table(const char *name, float (*fn)(float), const struct ref *t, int n,
                        double floor, double limit_eps) {
    double worst = 0, e;
    int i;
    for (i = 0; i < n; i++) {
        e = err_of(fn(t[i].x), t[i].y, floor);
        if (e > worst || e != e) worst = e;
    }
    console_set_color(worst <= limit_eps * EPS ? VGA_LIGHT_GREEN : VGA_LIGHT_RED);
    console_write(worst <= limit_eps * EPS ? "  [ok]   " : "  [FAIL] ");
    console_set_color(VGA_LIGHT_GREY);
    console_write(name);
    console_write(": ");
    console_write_dec((unsigned int) n);
    console_write(" points, max error ");
    console_write_float((float) (worst / EPS), 2);
    console_write(" eps (limit ");
    console_write_dec((unsigned int) limit_eps);
    console_write(")\n");
    if (worst <= limit_eps * EPS) passed++; else failed++;
}

#define N(t) ((int) (sizeof(t) / sizeof((t)[0])))

extern void xmm_interrupt_test(unsigned int wait_count, float *out, const float *in);


/* volatile：不让编译器在编译时就把结果算好——要的是运行时 CPU 真的去算 */
static volatile float zero = 0.0f, one = 1.0f, minus_one = -1.0f;

int mathtest_run(void) {
    static float in[32], out[32];
    float pw_worst = 0;
    unsigned int t0;
    int i, same;

    passed = failed = 0;
    if (!cpu_get_info()->float_enabled) {
        console_write("floating point is not enabled on this CPU\n");
        return 1;
    }

    check(one + one == 2.0f && one / 4.0f == 0.25f, "basic float arithmetic");
    check(km_isinf(one / zero) && one / zero > 0, "1/0 = +inf (no exception)");
    check(km_isnan(zero / zero), "0/0 = nan (no exception)");

    check_table("sqrtf", sqrtf, ref_sqrt, N(ref_sqrt), 1e-38, 1);
    check_table("expf ", expf,  ref_exp,  N(ref_exp),  1e-38, 4);
    check_table("logf ", logf,  ref_log,  N(ref_log),  1.0,   4);
    check_table("sinf ", sinf,  ref_sin,  N(ref_sin),  1.0,   4);
    check_table("cosf ", cosf,  ref_cos,  N(ref_cos),  1.0,   4);
    check_table("tanhf", tanhf, ref_tanh, N(ref_tanh), 1.0,   4);

    for (i = 0; i < N(ref_pow); i++) {
        float e = (float) (err_of(powf(ref_pow[i].x, ref_pow[i].y), ref_pow[i].z, 1e-38) / EPS);
        if (e > pw_worst || e != e) pw_worst = e;
    }
    console_write("  powf : max error ");
    console_write_float(pw_worst, 2);
    console_write(" eps\n");
    check(pw_worst <= 32, "powf within 32 eps (RoPE frequencies)");

    check(expf(-200) == 0 && km_isinf(expf(200)) && km_isinf(logf(zero)) && logf(zero) < 0
          && km_isnan(logf(minus_one)) && km_isnan(sqrtf(minus_one)), "special values (exp/log/sqrt)");

    /* 中断不破坏 XMM 寄存器：等 30 次中断（约 0.3 秒） */
    for (i = 0; i < 32; i++) in[i] = (float) i * 1.25f - 7.0f;
    t0 = pit_ticks();
    xmm_interrupt_test(30, out, in);
    same = 1;
    for (i = 0; i < 32; i++) if (out[i] != in[i]) same = 0;
    check(same && pit_ticks() - t0 >= 25, "xmm0-7 survive 30 timer interrupts");

    console_set_color(failed ? VGA_LIGHT_RED : VGA_LIGHT_GREEN);
    console_write("mathtest: ");
    console_write_dec((unsigned int) passed);
    console_putc('/');
    console_write_dec((unsigned int) (passed + failed));
    console_write(" passed\n");
    console_set_color(VGA_LIGHT_GREY);
    return failed;
}
