#include "kstring.h"

void *memset(void *dst, int c, unsigned int n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char) c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned int n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, unsigned int n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;   /* 目标在源后面：从后往前复制，重叠时才不会覆盖还没复制的部分 */
    }
    return dst;
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
