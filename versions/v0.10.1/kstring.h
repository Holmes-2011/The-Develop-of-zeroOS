#ifndef KSTRING_H
#define KSTRING_H

/* 最基本的内存/字符串函数。
   即使用了 -ffreestanding，编译器仍然可能自己生成对 memcpy / memset 的调用
   （比如复制一个大结构体、把一个大数组清零），所以内核必须提供它们 */

void        *memset(void *dst, int c, unsigned int n);
void        *memcpy(void *dst, const void *src, unsigned int n);
void        *memmove(void *dst, const void *src, unsigned int n);
int          memcmp(const void *a, const void *b, unsigned int n);
unsigned int strlen(const char *s);

#endif
