#include "demand.h"
#include "paging.h"
#include "pmm.h"
#include "telemetry.h"

static unsigned int mapped;

/* 在缺页异常处理里调用（中断已关）。
   错误码第 0 位 = 1 表示"页在、但权限不对"——那不是缺页，是真正的错误，不处理 */
int demand_handle_fault(unsigned int addr, unsigned int err_code) {
    unsigned int phys, *p;
    int i;
    if (addr < DEMAND_START || addr >= DEMAND_END || (err_code & 1)) return 0;
    phys = pmm_alloc_page();
    if (!phys) {
        tel_event(EV_DEMAND_OOM, addr);
        return 0;                                   /* 没内存了：按真正的缺页停机 */
    }
    p = (unsigned int *) phys;                      /* 物理内存是恒等映射的，可以直接写 */
    for (i = 0; i < 1024; i++) p[i] = 0;            /* 给出去的页必须是干净的，不能带着别人的旧数据 */
    if (paging_map(addr & ~0xFFFU, phys, PAGE_WRITABLE) != 0) {
        pmm_free_page(phys);
        tel_event(EV_DEMAND_OOM, addr);
        return 0;
    }
    mapped++;
    tel_demand_fault(addr);
    return 1;
}

void demand_release(void) {
    unsigned int v;
    for (v = DEMAND_START; v < DEMAND_END && mapped; v += PAGE_SIZE) {
        unsigned int phys = paging_translate(v);
        if (phys == 0xFFFFFFFF) continue;
        paging_unmap(v);
        pmm_free_page(phys & ~0xFFFU);
        mapped--;
    }
}

unsigned int demand_pages(void) { return mapped; }
