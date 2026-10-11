#ifndef PMM_H
#define PMM_H

/* 物理内存管理（Physical Memory Manager）：以 4KB 的"页"为单位，记录哪些物理内存空着。
   用一张位图：每一页对应 1 个比特，1 = 已占用 / 不可用，0 = 空闲。
   哪些内存能用，来自引导扇区向 BIOS 要的 E820 内存地图（见 boot_info.h） */

#define PAGE_SIZE 4096

/* 只用这个地址以下的物理内存（3GB）。
   原因：物理内存是恒等映射的（虚拟地址 = 物理地址），而 0xC0000000 以上的虚拟地址
   留给内核自己用（0xD0000000 起是内核堆）。两者不能重叠。
   32 位系统本来也只能寻址 4GB，其中最上面一段通常被显卡、PCI 设备占着 */
#define PHYS_LIMIT 0xC0000000U

void         pmm_init(void);
unsigned int pmm_alloc_page(void);           /* 返回一页的物理地址；没内存了返回 0 */
void         pmm_free_page(unsigned int addr);

unsigned int pmm_total_pages(void);          /* 可用内存一共多少页（不含被保留的） */
unsigned int pmm_free_pages(void);           /* 现在还空着多少页 */
unsigned int pmm_max_addr(void);             /* 最高的可用物理地址（不含），分页要映射到这里 */
unsigned int pmm_bitmap_start(void);
unsigned int pmm_bitmap_end(void);           /* 位图末尾（4KB 对齐）；1MB 到这里都被内核占着 */

#endif
