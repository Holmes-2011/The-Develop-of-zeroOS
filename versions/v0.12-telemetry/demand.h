#ifndef DEMAND_H
#define DEMAND_H

/* 按需分页区（v0.11）：0xC0000000 ~ 0xC4000000 这 64MB 虚拟地址一开始什么都没映射。
   第一次访问其中某一页时 CPU 报缺页（#PF），内核不再停机，而是现场分配一页物理内存、
   清零、映射上去，然后让那条指令重新执行——程序完全感觉不到。

   这是两件事的第一步：
     * 以后真正的按需分页（进程用多少内存才给多少）
     * 缺页成了可以观测、可以计数的信号（感知层的 demand_faults），
       而不是"一出现就停机"

   其他地址的缺页照旧停机（那是真正的错误，比如空指针）。 */

#define DEMAND_START 0xC0000000U
#define DEMAND_END   0xC4000000U

int          demand_handle_fault(unsigned int addr, unsigned int err_code);  /* 处理掉了返回 1 */
void         demand_release(void);   /* 解除整个区域的映射，物理页全部还回去 */
unsigned int demand_pages(void);     /* 现在映射了几页 */

#endif
