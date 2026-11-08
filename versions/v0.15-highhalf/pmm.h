#ifndef PMM_H
#define PMM_H

/* 物理内存管理（Physical Memory Manager）：以 4KB 的"页"为单位，记录哪些物理内存空着。
   用一张位图：每一页对应 1 个比特，1 = 已占用 / 不可用，0 = 空闲。
   哪些内存能用，来自引导扇区向 BIOS 要的 E820 内存地图（见 boot_info.h） */

#define PAGE_SIZE 4096

/* 只用这个地址以下的物理内存（768MB）。
   v0.15 起物理内存整体映射到虚拟地址 0xC0000000 ~ 0xEFFFFFFF（见 paging.h），只有这么宽的窗口。
   更多内存要么换成按需映射，要么上 64 位 */
#define PHYS_LIMIT 0x30000000U

void         pmm_init(void);
unsigned int pmm_alloc_page(void);           /* 返回一页的物理地址；没内存了返回 0 */
void         pmm_free_page(unsigned int addr);

unsigned int pmm_total_pages(void);          /* 可用内存一共多少页（不含被保留的） */
unsigned int pmm_free_pages(void);           /* 现在还空着多少页 */
unsigned int pmm_max_addr(void);             /* 最高的可用物理地址（不含），分页要映射到这里 */
unsigned int pmm_bitmap_start(void);          /* 位图的物理地址 */
unsigned int pmm_bitmap_end(void);            /* 位图末尾（物理，4KB 对齐）；1MB 到这里都被内核占着 */

/* 一致性检查（v0.13，工具层的 pmm_check）：重新数一遍位图里的空闲页，和 free_pages 计数器比。
   返回 0 一致；-1 不一致。*counted 是数出来的，*claimed 是计数器 */
int          pmm_check(unsigned int *counted, unsigned int *claimed);           /* 位图末尾（4KB 对齐）；1MB 到这里都被内核占着 */

#endif
