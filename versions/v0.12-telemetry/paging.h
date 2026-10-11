#ifndef PAGING_H
#define PAGING_H

/* 分页（x86 32 位，两级页表，每页 4KB）。
   打开分页之后，CPU 看到的每个地址（虚拟地址）都要先查页表，换算成真正的物理地址。

   zeroOS 的布局：
     * 0 ~ 最高可用物理地址：恒等映射（虚拟地址 = 物理地址），内核照常访问一切
     * 第 0 页（0 ~ 4KB）故意不映射：空指针一解引用就触发 #PF，而不是悄悄读到垃圾
     * 0xD0000000 起：内核堆（heap.c），按需从物理内存管理那里要页，映射过来 */

#define PAGE_PRESENT  0x1
#define PAGE_WRITABLE 0x2

void         paging_init(void);                                   /* 建表 + 打开分页 */
int          paging_map(unsigned int virt, unsigned int phys, unsigned int flags);   /* 0 成功，-1 没内存 */
void         paging_unmap(unsigned int virt);
unsigned int paging_translate(unsigned int virt);                 /* 查表换算；没映射返回 0xFFFFFFFF */
unsigned int paging_table_count(void);                            /* 一共建了几张页表 */

#endif
