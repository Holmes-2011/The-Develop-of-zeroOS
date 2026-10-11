#include "pmm.h"
#include "boot_info.h"
#include "console.h"

extern char __kernel_end[];   /* link.ld 导出：内核（含 .bss）在内存里的末端 */

#define LOW_MEMORY_END 0x100000   /* 1MB 以下全部保留：中断向量表、BIOS 数据、显存、BIOS ROM…… */

static unsigned int *bitmap;      /* 第 n 页 = bitmap[n / 32] 的第 (n % 32) 位 */
static unsigned int total_frames; /* 位图覆盖的页数 = max_addr / 4KB */
static unsigned int usable_pages;
static unsigned int free_pages;
static unsigned int max_addr;
static unsigned int bitmap_end;
static unsigned int search_hint;  /* 从这个下标开始找空页，不用每次都从头扫 */

static int  test_bit(unsigned int f) { return (bitmap[f / 32] >> (f % 32)) & 1; }
static void set_bit(unsigned int f)  { bitmap[f / 32] |=  (1U << (f % 32)); }
static void clear_bit(unsigned int f){ bitmap[f / 32] &= ~(1U << (f % 32)); }

/* E820 的地址是 64 位的；zeroOS 只用 3GB 以下的物理内存（PHYS_LIMIT，见 pmm.h）。
   把一条记录裁成 [*start, *end)，超出的部分直接丢掉；整条都超出就返回 0 */
static int entry_range(const struct e820_entry *e, unsigned long long *start, unsigned long long *end) {
    unsigned long long base = ((unsigned long long) e->base_hi << 32) | e->base_lo;
    unsigned long long len  = ((unsigned long long) e->len_hi  << 32) | e->len_lo;
    const unsigned long long limit = PHYS_LIMIT;
    if (len == 0 || base >= limit) return 0;
    *start = base;
    *end = base + len > limit ? limit : base + len;
    return 1;
}

/* 把 [start, end) 这段内存里的页标成"占用"（向外取整：沾到一点就算占用） */
/* 注意：64 位的除法会被编译成对 __udivdi3 的调用，而裸机内核没有这个库函数，
   所以 64 位的数一律用移位（>> 12 就是 ÷ 4096） */
static void reserve_range(unsigned long long start, unsigned long long end) {
    unsigned int f = (unsigned int) (start >> 12);
    unsigned int last = (unsigned int) ((end + PAGE_SIZE - 1) >> 12);
    for (; f < last && f < total_frames; f++) {
        if (!test_bit(f)) {
            set_bit(f);
            free_pages--;
            usable_pages--;
        }
    }
}

static void panic(const char *msg) {
    console_set_color(VGA_LIGHT_RED);
    console_write(msg);
    console_write("\nsystem halted.\n");
    while (1) __asm__ __volatile__("cli; hlt");
}

void pmm_init(void) {
    const struct e820_entry *map = (const struct e820_entry *) E820_MAP_ADDR;
    unsigned int count = *(const unsigned short *) E820_COUNT_ADDR;
    unsigned long long start, end;
    unsigned int i, f, words;

    if (count == 0) panic("pmm: BIOS gave no E820 memory map");
    if (count > E820_MAX) count = E820_MAX;

    /* 1. 最高的可用地址决定位图有多大 */
    for (i = 0; i < count; i++) {
        if (map[i].type == E820_USABLE && entry_range(&map[i], &start, &end) && end > max_addr) {
            max_addr = (unsigned int) end;
        }
    }
    max_addr &= ~(PAGE_SIZE - 1);
    total_frames = max_addr / PAGE_SIZE;

    /* 2. 位图紧跟在内核后面（4KB 对齐），先全部标成"占用" */
    bitmap = (unsigned int *) (((unsigned int) __kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    words = (total_frames + 31) / 32;
    bitmap_end = ((unsigned int) bitmap + words * 4 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (bitmap_end > max_addr) panic("pmm: not enough memory for the page bitmap");
    for (i = 0; i < words; i++) bitmap[i] = 0xFFFFFFFF;

    /* 3. E820 说可用的页放开（向内取整：不完整的页不要） */
    for (i = 0; i < count; i++) {
        if (map[i].type != E820_USABLE || !entry_range(&map[i], &start, &end)) continue;
        f = (unsigned int) ((start + PAGE_SIZE - 1) >> 12);
        for (; f < (unsigned int) (end >> 12) && f < total_frames; f++) {
            if (test_bit(f)) {
                clear_bit(f);
                free_pages++;
                usable_pages++;
            }
        }
    }

    /* 4. 再收回不能给别人用的：
          E820 里标成保留的（记录可能互相重叠，规范要求以"保留"为准）、
          1MB 以下、内核本身 + 位图 */
    for (i = 0; i < count; i++) {
        if (map[i].type != E820_USABLE && entry_range(&map[i], &start, &end)) {
            reserve_range(start, end);
        }
    }
    reserve_range(0, LOW_MEMORY_END);
    reserve_range(LOW_MEMORY_END, bitmap_end);

    search_hint = bitmap_end / PAGE_SIZE;
}

unsigned int pmm_alloc_page(void) {
    unsigned int f = search_hint, n = 0;
    if (free_pages == 0) return 0;
    while (n < total_frames) {               /* 最多绕一圈 */
        if (f >= total_frames) f = 0;
        if (f % 32 == 0 && bitmap[f / 32] == 0xFFFFFFFF) {
            f += 32;                         /* 这 32 页全满，整个跳过 */
            n += 32;
            continue;
        }
        if (!test_bit(f)) {
            set_bit(f);
            free_pages--;
            search_hint = f + 1;
            return f * PAGE_SIZE;
        }
        f++;
        n++;
    }
    return 0;   /* 页 0 永远在 1MB 以下的保留区里，所以 0 可以安全地当"失败" */
}

void pmm_free_page(unsigned int addr) {
    unsigned int f = addr / PAGE_SIZE;
    if ((addr & (PAGE_SIZE - 1)) || f >= total_frames || addr < bitmap_end) {
        console_write("pmm: refusing to free invalid page ");
        console_write_hex(addr);
        console_putc('\n');
        return;
    }
    if (!test_bit(f)) {
        console_write("pmm: double free of page ");
        console_write_hex(addr);
        console_putc('\n');
        return;
    }
    clear_bit(f);
    free_pages++;
    if (f < search_hint) search_hint = f;
}

unsigned int pmm_total_pages(void)  { return usable_pages; }
unsigned int pmm_free_pages(void)   { return free_pages; }
unsigned int pmm_max_addr(void)     { return max_addr; }
unsigned int pmm_bitmap_start(void) { return (unsigned int) bitmap; }
unsigned int pmm_bitmap_end(void)   { return bitmap_end; }
