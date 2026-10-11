#include "memtest.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "console.h"

static int passed, failed;

static void check(int ok, const char *what) {
    if (ok) {
        console_set_color(VGA_LIGHT_GREEN);
        console_write("  [ok]   ");
        passed++;
    } else {
        console_set_color(VGA_LIGHT_RED);
        console_write("  [FAIL] ");
        failed++;
    }
    console_set_color(VGA_LIGHT_GREY);
    console_write(what);
    console_putc('\n');
}

/* ---------------- 物理页 ---------------- */
#define NPAGES 64

static void test_pmm(void) {
    static unsigned int pages[NPAGES];
    unsigned int before = pmm_free_pages();
    int i, j, ok;

    for (i = 0; i < NPAGES; i++) pages[i] = pmm_alloc_page();

    ok = 1;
    for (i = 0; i < NPAGES; i++) {
        if (!pages[i] || (pages[i] & 0xFFF) || pages[i] < pmm_bitmap_end() || pages[i] >= pmm_max_addr()) ok = 0;
        for (j = 0; j < i; j++) if (pages[j] == pages[i]) ok = 0;
    }
    check(ok, "pmm: 64 pages are aligned, distinct, above the kernel");
    check(pmm_free_pages() == before - NPAGES, "pmm: free count dropped by 64");

    /* 每页写满不同的内容，再全部读回来：页之间不能互相覆盖 */
    for (i = 0; i < NPAGES; i++) {
        unsigned int *p = (unsigned int *) pages[i];
        for (j = 0; j < 1024; j++) p[j] = pages[i] ^ (unsigned int) j;
    }
    ok = 1;
    for (i = 0; i < NPAGES; i++) {
        unsigned int *p = (unsigned int *) pages[i];
        for (j = 0; j < 1024; j++) if (p[j] != (pages[i] ^ (unsigned int) j)) ok = 0;
    }
    check(ok, "pmm: 256 KB written and read back intact");

    for (i = 0; i < NPAGES; i++) pmm_free_page(pages[i]);
    check(pmm_free_pages() == before, "pmm: all pages returned");

    console_write("  (a 'double free' warning is expected next)\n");
    pmm_free_page(pages[0]);
    check(pmm_free_pages() == before, "pmm: double free detected, count unchanged");
}

/* ---------------- 分页 ---------------- */
#define TEST_VIRT 0xE0000000   /* 在堆的范围之外，专门给这个测试用 */

static void test_paging(void) {
    unsigned int phys = pmm_alloc_page();
    volatile unsigned int *virt = (volatile unsigned int *) TEST_VIRT;

    check(paging_translate(0) == 0xFFFFFFFF, "paging: page 0 is unmapped (null pointers fault)");
    check(paging_translate(0xB8000) == 0xB8000, "paging: VGA memory is identity-mapped");

    check(phys && paging_map(TEST_VIRT, phys, PAGE_WRITABLE) == 0
          && paging_translate(TEST_VIRT + 0x123) == phys + 0x123, "paging: map 0xE0000000 to a fresh page");

    /* 从虚拟地址写，从物理地址（恒等映射）读：确认两个地址真的是同一块内存 */
    virt[0] = 0xC0FFEE00;
    virt[1023] = 0x5A0A0000;
    check(((unsigned int *) phys)[0] == 0xC0FFEE00 && ((unsigned int *) phys)[1023] == 0x5A0A0000,
          "paging: write via virtual address, read via physical");

    paging_unmap(TEST_VIRT);
    check(paging_translate(TEST_VIRT) == 0xFFFFFFFF, "paging: unmap");
    pmm_free_page(phys);
}

/* ---------------- 内核堆 ---------------- */
#define NBLOCKS 40

static int filled_ok(const unsigned char *p, unsigned int n, unsigned char v) {
    unsigned int i;
    for (i = 0; i < n; i++) if (p[i] != v) return 0;
    return 1;
}

static void fill(unsigned char *p, unsigned int n, unsigned char v) {
    unsigned int i;
    for (i = 0; i < n; i++) p[i] = v;
}

static void test_heap(void) {
    static unsigned char *blk[NBLOCKS];
    static unsigned int size[NBLOCKS];
    struct heap_stats st, before;
    unsigned char *big;
    int i, ok;

    heap_get_stats(&before);
    check(kmalloc(0) == 0, "heap: kmalloc(0) returns null");

    ok = 1;
    for (i = 0; i < NBLOCKS; i++) {
        size[i] = (unsigned int) (i * 37 % 500) + 1;   /* 1 ~ 500 字节，各不相同 */
        blk[i] = kmalloc(size[i]);
        if (!blk[i] || ((unsigned int) blk[i] & 7) || (unsigned int) blk[i] < HEAP_START) ok = 0;
        else fill(blk[i], size[i], (unsigned char) i);
    }
    check(ok, "heap: 40 blocks of 1..500 bytes, 8-byte aligned");

    /* 全部填完再统一检查：如果有两块重叠，先填的那块会被后填的覆盖 */
    ok = 1;
    for (i = 0; i < NBLOCKS; i++) if (blk[i] && !filled_ok(blk[i], size[i], (unsigned char) i)) ok = 0;
    check(ok, "heap: no two blocks overlap");

    /* 释放一半，再分配一批：应该复用刚释放的空间 */
    for (i = 0; i < NBLOCKS; i += 2) kfree(blk[i]);
    ok = 1;
    for (i = 0; i < NBLOCKS; i += 2) {
        size[i] = 24;
        blk[i] = kmalloc(size[i]);
        if (!blk[i]) ok = 0;
        else fill(blk[i], size[i], (unsigned char) (100 + i));
    }
    for (i = 0; i < NBLOCKS; i++) {
        if (blk[i] && !filled_ok(blk[i], size[i], (unsigned char) (i % 2 ? i : 100 + i))) ok = 0;
    }
    check(ok, "heap: free half, reallocate, all contents intact");

    for (i = 0; i < NBLOCKS; i++) kfree(blk[i]);
    heap_get_stats(&st);
    check(st.used == before.used && st.blocks == before.blocks,
          "heap: everything freed and merged back together");

    big = kmalloc(100 * 1024);
    ok = big != 0;
    if (ok) {
        fill(big, 100 * 1024, 0x5A);
        ok = filled_ok(big, 100 * 1024, 0x5A);
    }
    heap_get_stats(&st);
    check(ok && st.mapped >= 100 * 1024, "heap: 100 KB block (heap grows by mapping new pages)");
    kfree(big);

    /* 两块空闲块合并后，被吞掉的块头会作废，所以这里报的可能是 "invalid pointer" */
    console_write("  (a heap warning is expected next)\n");
    kfree(big);
    heap_get_stats(&st);
    check(st.used == before.used, "heap: double free detected, heap unchanged");
}

int memtest_run(void) {
    passed = failed = 0;
    test_pmm();
    test_paging();
    test_heap();
    if (failed == 0) console_set_color(VGA_LIGHT_GREEN);
    else console_set_color(VGA_LIGHT_RED);
    console_write("memtest: ");
    console_write_dec((unsigned int) passed);
    console_putc('/');
    console_write_dec((unsigned int) (passed + failed));
    console_write(" passed\n");
    console_set_color(VGA_LIGHT_GREY);
    return failed;
}
