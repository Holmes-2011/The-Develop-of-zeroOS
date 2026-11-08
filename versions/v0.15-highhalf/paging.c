#include "paging.h"
#include "pmm.h"
#include "console.h"

/* 页目录 1024 项，每项管 4MB（指向一张页表）；页表 1024 项，每项管 4KB。
   虚拟地址的高 10 位查页目录，中间 10 位查页表，低 12 位是页内偏移 */
#define PD_INDEX(v) ((v) >> 22)
#define PT_INDEX(v) (((v) >> 12) & 0x3FF)

static unsigned int page_directory_phys;   /* CR3 里的那个值 */
static unsigned int *page_directory;       /* 它的可访问指针 = P2V */
static unsigned int table_count;

static void zero_page(unsigned int *p) {
    int i;
    for (i = 0; i < 1024; i++) p[i] = 0;
}

/* 页表本身放在物理内存里；v0.15 起通过 P2V() 访问（以前是恒等映射，物理地址就能直接用） */
static unsigned int *get_table(unsigned int virt, int create) {
    unsigned int pde = page_directory[PD_INDEX(virt)];
    unsigned int table;
    if (pde & PAGE_PRESENT) return (unsigned int *) P2V(pde & ~0xFFFU);
    if (!create) return 0;
    table = pmm_alloc_page();
    if (!table) return 0;
    zero_page((unsigned int *) P2V(table));
    /* 用户态范围（< KERNEL_VBASE）的页目录项带 USER 位，具体页能不能给 ring 3 由页表项决定 */
    page_directory[PD_INDEX(virt)] = table | PAGE_PRESENT | PAGE_WRITABLE | (virt < KERNEL_VBASE ? PAGE_USER : 0);
    table_count++;
    return (unsigned int *) P2V(table);
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
    int i;
    if (!table) return;
    table[PT_INDEX(virt)] = 0;
    invlpg(virt);
    /* 这张页表全空了就连它一起还回去（v0.13.2） */
    for (i = 0; i < 1024; i++) if (table[i] & PAGE_PRESENT) return;
    page_directory[PD_INDEX(virt)] = 0;
    pmm_free_page(V2P(table));
    table_count--;
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
unsigned int paging_directory_phys(void) { return page_directory_phys; }

static void panic(const char *msg, unsigned int addr) {
    console_set_color(VGA_LIGHT_RED);
    console_write(msg);
    console_write_hex(addr);
    console_write("\nsystem halted.\n");
    while (1) __asm__ __volatile__("cli; hlt");
}

void paging_init(void) {
    unsigned int phys, end = pmm_max_addr();

    page_directory_phys = pmm_alloc_page();
    if (!page_directory_phys) panic("paging: no page for the page directory", 0);
    page_directory = (unsigned int *) P2V(page_directory_phys);
    zero_page(page_directory);

    /* 物理内存的直接映射：物理 [0, end) → 虚拟 [KERNEL_VBASE, KERNEL_VBASE + end)。
       内核本身（物理 1MB 起）、显存 0xB8000、BIOS 数据区都在里面。
       虚拟 0 ~ 3GB 一个都不映射：第 0 页空指针照样缺页，其余留给用户态（v0.16） */
    for (phys = 0; phys < end; phys += PAGE_SIZE) {
        if (paging_map(KERNEL_VBASE + phys, phys, PAGE_WRITABLE) != 0) {
            /* 建页表时物理页就用完了（内存极小）：留着没映射的洞，之后会在莫名其妙的地方缺页；不如现在就停 */
            panic("paging: out of memory while mapping physical memory at ", phys);
        }
    }

    /* 换掉 entry.asm 的临时页表（分页已经开着，只换 CR3） */
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(page_directory_phys) : "memory");
}
