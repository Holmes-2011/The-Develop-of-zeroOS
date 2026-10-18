#ifndef STACK_H
#define STACK_H

/* 内核栈的水位和溢出检测（v0.13.2）。
   entry.asm 开机时把整个栈填成图案 0x5AC0FFEE；栈从高地址往低地址用，
   从栈底往上找到第一个不是图案的字，就是历史最深处。栈底第一个字是哨兵 */

#define STACK_PATTERN 0x5AC0FFEEU

unsigned int stack_size(void);        /* 总大小（字节） */
unsigned int stack_high_water(void);  /* 开机以来最多用过多少字节 */
int          stack_overflowed(void);  /* 哨兵被覆盖了 = 1 */
unsigned int stack_base(void);        /* 栈底地址 */

#endif
