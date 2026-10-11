#ifndef HEAP_H
#define HEAP_H

/* 内核堆：kmalloc / kfree，大小任意（按 8 字节对齐）。
   堆在虚拟地址 0xD0000000 起，不够用时从物理内存管理要新页、用分页映射到堆的末尾，
   所以堆在虚拟地址上连续，背后的物理页可以东一块西一块。

   每块前面有 16 字节的块头：魔数 + 大小 + 前后指针。魔数用来抓两类常见错误：
   释放同一块两次、释放一个不是 kmalloc 给的地址（或者块头被写坏了） */

#define HEAP_START 0xD0000000
#define HEAP_LIMIT 0xE0000000   /* 堆最多长到 256MB */

void  heap_init(void);
void *kmalloc(unsigned int size);   /* 失败返回 0；kmalloc(0) 也返回 0 */
void  kfree(void *ptr);             /* kfree(0) 什么都不做 */

struct heap_stats {
    unsigned int mapped;      /* 堆一共映射了多少字节 */
    unsigned int used;        /* 已分配给调用方的字节数（不含块头） */
    unsigned int free;        /* 空闲块里可用的字节数 */
    unsigned int blocks;      /* 一共几块 */
    unsigned int free_blocks; /* 其中几块是空闲的 */
};
void heap_get_stats(struct heap_stats *st);

#endif
