#include "kstring.h"

/* v0.13.2 起用 CPU 的块传输指令（rep movsb / rep stosb）：
   比逐字节循环快得多（现代 CPU 对 rep movsb 有专门优化），
   而且编译器绝不会把这样的函数又"优化"成对 memcpy 自己的调用（裸机上经典的无限递归坑） */

void *memset(void *dst, int c, unsigned int n) {
    void *d = dst;
    __asm__ __volatile__("cld; rep stosb" : "+D"(d), "+c"(n) : "a"((unsigned char) c) : "memory");
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned int n) {
    void *d = dst;
    const void *s = src;
    __asm__ __volatile__("cld; rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return dst;
}

void *memmove(void *dst, const void *src, unsigned int n) {
    if (dst < src || (const char *) dst >= (const char *) src + n) {
        return memcpy(dst, src, n);         /* 不重叠，或目标在源前面：正向复制就安全 */
    } else {
        /* 目标在源后面且重叠：从末尾往前复制（方向标志置 1），复制完必须把方向标志清回去 */
        void *d = (char *) dst + n - 1;
        const void *s = (const char *) src + n - 1;
        __asm__ __volatile__("std; rep movsb; cld" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
        return dst;
    }
}

int memcmp(const void *a, const void *b, unsigned int n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) {
        if (*x != *y) return *x - *y;
    }
    return 0;
}

unsigned int strlen(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}
