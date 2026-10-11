#include "paging.h"
#include "pmm.h"

/* 页目录 1024 项，每项管 4MB（指向一张页表）；页表 1024 项，每项管 4KB。
   虚拟地址的高 10 位查页目录，中间 10 位查页表，低 12 位是页内偏移 */
#define PD_INDEX(v) ((v) >> 22)
#define PT_INDEX(v) (((v) >> 12) & 0x3FF)

static unsigned int *page_directory;
static unsigned int table_count;

static void zero_page(unsigned int *p) {
    int i;
    for (i = 0; i < 1024; i++) p[i] = 0;
}

/* 页表本身也放在物理内存里，并且都在恒等映射的范围内，
   所以分页打开前后都能用同一个地址直接访问它们 */
static unsigned int *get_table(unsigned int virt, int create) {
    unsigned int pde = page_directory[PD_INDEX(virt)];
    unsigned int table;
    if (pde & PAGE_PRESENT) return (unsigned int *) (pde & ~0xFFFU);
    if (!create) return 0;
    table = pmm_alloc_page();
    if (!table) return 0;
    zero_page((unsigned int *) table);
    page_directory[PD_INDEX(virt)] = table | PAGE_PRESENT | PAGE_WRITABLE;
    table_count++;
    return (unsigned int *) table;
}

static void invlpg(unsigned int virt) {
    /* CPU 会缓存查过的页表结果（TLB）；改了映射必须让它忘掉这一页 */
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt) : "memory");
}

int paging_map(unsigned int virt, unsigned int phys, unsigned int flags) {
    unsigned int *table = get_table(virt, 1);
    if (!table) return -1;
    table[PT_INDEX(virt)] = (phys & ~0xFFFU) | (flags & 0xFFF) | PAGE_PRESENT;
    invlpg(virt);
    return 0;
}

void paging_unmap(unsigned int virt) {
    unsigned int *table = get_table(virt, 0);
    if (!table) return;
    table[PT_INDEX(virt)] = 0;
    invlpg(virt);
}

unsigned int paging_translate(unsigned int virt) {
    unsigned int *table = get_table(virt, 0);
    unsigned int pte;
    if (!table) return 0xFFFFFFFF;
    pte = table[PT_INDEX(virt)];
    if (!(pte & PAGE_PRESENT)) return 0xFFFFFFFF;
    return (pte & ~0xFFFU) | (virt & 0xFFF);
}

unsigned int paging_table_count(void) { return table_count; }

void paging_init(void) {
    unsigned int addr, end = pmm_max_addr();

    page_directory = (unsigned int *) pmm_alloc_page();
    zero_page(page_directory);

    /* 恒等映射：从第 1 页开始（跳过第 0 页），一直到最高的可用物理地址。
       显存 0xB8000、BIOS 区这些 1MB 以下的地址也在里面 */
    for (addr = PAGE_SIZE; addr < end; addr += PAGE_SIZE) {
        paging_map(addr, addr, PAGE_WRITABLE);
    }

    /* CR3 = 页目录的物理地址；CR0 的第 31 位（PG）= 打开分页 */
    __asm__ __volatile__(
        "mov %0, %%cr3\n\t"
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t"
        "mov %%eax, %%cr0"
        : : "r"(page_directory) : "eax", "memory");
}
