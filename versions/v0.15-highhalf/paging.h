#ifndef PAGING_H
#define PAGING_H

/* 分页（x86 32 位，两级页表，每页 4KB）。
   打开分页之后，CPU 看到的每个地址（虚拟地址）都要先查页表，换算成真正的物理地址。

   v0.15 以前内核是恒等映射（虚拟 = 物理），把 0 ~ 3GB 全占了，用户态没地方放。
   现在物理内存整体映射到 0xC0000000 起（见下面的布局），内核通过 P2V() 访问任何物理页；
   页表的物理地址和"能直接访问的指针"从此是两个东西。 */

#define PAGE_PRESENT  0x1
#define PAGE_WRITABLE 0x2
#define PAGE_USER     0x4   /* ring 3 可访问（v0.16 起用） */

/* 高半内核（v0.15）：虚拟地址空间的布局
     0x00000000 ~ 0xBFFFFFFF  用户态（现在空着；第 0 页永远不映射）
     0xC0000000 ~ 0xEFFFFFFF  物理内存的直接映射：虚拟 = 物理 + KERNEL_VBASE，最多 768MB（PHYS_LIMIT）
                              内核本身在 0xC0100000（物理 1MB）
     0xF0000000 ~ 0xF7FFFFFF  内核堆（heap.h）
     0xF8000000 ~ 0xFBFFFFFF  按需分页区（demand.h）
     0xFC000000 ~ 0xFFFFFFFF  留着（memtest 用 0xFC000000 测映射） */
#define KERNEL_VBASE 0xC0000000U
#define P2V(p) ((void *) ((unsigned int) (p) + KERNEL_VBASE))      /* 物理地址 → 可以直接访问的虚拟地址 */
#define V2P(v) ((unsigned int) (v) - KERNEL_VBASE)                /* 只对直接映射区（含内核本身）有效 */

void         paging_init(void);                                   /* 建表 + 打开分页 */
int          paging_map(unsigned int virt, unsigned int phys, unsigned int flags);   /* 0 成功，-1 没内存 */
void         paging_unmap(unsigned int virt);                     /* 页表因此空了的话连页表一起释放 */
unsigned int paging_translate(unsigned int virt);                 /* 查表换算；没映射返回 0xFFFFFFFF */
unsigned int paging_table_count(void);                            /* 一共建了几张页表 */
unsigned int paging_directory_phys(void);                         /* 当前页目录的物理地址（CR3） */

#endif
