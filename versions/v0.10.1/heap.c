#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "console.h"

#define MAGIC_USED 0x5A0A110C   /* "zeroOS alloc" */
#define MAGIC_FREE 0x5A0AF4EE   /* "zeroOS free"  */
#define MIN_SPLIT  16           /* 切完剩下的部分至少要能放 16 字节，否则不切 */

struct block {
    unsigned int  magic;
    unsigned int  size;        /* 数据区大小（不含块头） */
    struct block *prev;        /* 按地址顺序的前一块 / 后一块 */
    struct block *next;
};

static struct block *first;
static void shrink(void);   /* 定义在 kfree 后面 */
static unsigned int heap_end;  /* 已映射部分的末尾 */

#define HDR ((unsigned int) sizeof(struct block))
#define DATA(b)  ((void *) ((char *) (b) + HDR))
#define BLOCK(p) ((struct block *) ((char *) (p) - HDR))
#define END(b)   ((unsigned int) (b) + HDR + (b)->size)

/* 往堆末尾再映射 bytes 字节（向上取整到页） */
static int grow(unsigned int bytes) {
    unsigned int target = (heap_end + bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (target > HEAP_LIMIT || target < heap_end) return -1;
    while (heap_end < target) {
        unsigned int phys = pmm_alloc_page();
        if (!phys) return -1;
        if (paging_map(heap_end, phys, PAGE_WRITABLE) != 0) {
            pmm_free_page(phys);
            return -1;
        }
        heap_end += PAGE_SIZE;
    }
    return 0;
}

void heap_init(void) {
    heap_end = HEAP_START;
    if (grow(PAGE_SIZE) != 0) return;
    first = (struct block *) HEAP_START;
    first->magic = MAGIC_FREE;
    first->size = PAGE_SIZE - HDR;
    first->prev = 0;
    first->next = 0;
}

/* 把 b 切成两块：前面 size 字节留给 b，剩下的成为一个新的空闲块 */
static void split(struct block *b, unsigned int size) {
    struct block *rest;
    if (b->size < size + HDR + MIN_SPLIT) return;
    rest = (struct block *) ((char *) DATA(b) + size);
    rest->magic = MAGIC_FREE;
    rest->size = b->size - size - HDR;
    rest->prev = b;
    rest->next = b->next;
    if (b->next) b->next->prev = rest;
    b->next = rest;
    b->size = size;
}

/* b 和它后面那块合并（调用方保证两块都空闲且相邻） */
static void merge_next(struct block *b) {
    struct block *n = b->next;
    b->size += HDR + n->size;
    b->next = n->next;
    if (n->next) n->next->prev = b;
    n->magic = 0;   /* 被吞掉的块头作废，防止有人拿着旧指针再 kfree 一次时被当成合法块 */
}

void *kmalloc(unsigned int size) {
    struct block *b, *last = 0;
    if (size == 0 || size > HEAP_LIMIT - HEAP_START) return 0;
    size = (size + 7) & ~7U;

    /* 首次适配：从头找第一块够大的空闲块 */
    for (b = first; b; b = b->next) {
        if (b->magic == MAGIC_FREE && b->size >= size) {
            split(b, size);
            b->magic = MAGIC_USED;
            return DATA(b);
        }
        last = b;
    }

    /* 没有够大的：把堆往后长 */
    if (!last) return 0;   /* heap_init 失败过 */
    {
        unsigned int old_end = heap_end;
        unsigned int need = size + HDR;
        if (last->magic == MAGIC_FREE) need -= last->size + HDR;   /* 最后一块空闲：只需补差额 */
        if (grow(need) != 0) return 0;
        if (last->magic == MAGIC_FREE) {
            last->size += heap_end - old_end;   /* 新页直接并进最后那块 */
            b = last;
        } else {
            b = (struct block *) old_end;
            b->magic = MAGIC_FREE;
            b->size = heap_end - old_end - HDR;
            b->prev = last;
            b->next = 0;
            last->next = b;
        }
        split(b, size);
        b->magic = MAGIC_USED;
        return DATA(b);
    }
}

void kfree(void *ptr) {
    struct block *b;
    int in_heap;
    if (!ptr) return;
    b = BLOCK(ptr);
    /* 先确认在堆里再读块头：堆外的地址可能根本没映射，读一下就是 #PF */
    in_heap = (unsigned int) ptr >= HEAP_START + HDR && (unsigned int) ptr < heap_end;
    if (!in_heap || b->magic != MAGIC_USED) {
        console_set_color(VGA_LIGHT_RED);
        console_write(in_heap && b->magic == MAGIC_FREE
                      ? "heap: double free of " : "heap: kfree of invalid pointer ");
        console_write_hex((unsigned int) ptr);
        console_set_color(VGA_LIGHT_GREY);
        console_putc('\n');
        return;
    }
    b->magic = MAGIC_FREE;
    /* 和前后相邻的空闲块合并，避免堆被切成一堆小碎片 */
    if (b->next && b->next->magic == MAGIC_FREE) merge_next(b);
    if (b->prev && b->prev->magic == MAGIC_FREE) merge_next(b->prev);
    shrink();
}

/* 堆末尾的空闲块太大时，把多出 HEAP_KEEP 的页解除映射、还给物理页分配器。
   v0.10 以前堆只长不缩：跑一次 bench（申请约 6MB）之后，这 6MB 就一直被堆占着 */
static void shrink(void) {
    struct block *last = first;
    unsigned int keep_end, v;
    while (last->next) last = last->next;
    if (last->magic != MAGIC_FREE) return;

    /* 至少留住最后这块的块头 + 一点数据区（页对齐），再加 HEAP_KEEP 的余量 */
    keep_end = (((unsigned int) DATA(last) + MIN_SPLIT + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)) + HEAP_KEEP;
    if (keep_end >= heap_end) return;

    for (v = keep_end; v < heap_end; v += PAGE_SIZE) {
        unsigned int phys = paging_translate(v);
        paging_unmap(v);
        if (phys != 0xFFFFFFFF) pmm_free_page(phys);
    }
    last->size = keep_end - (unsigned int) DATA(last);
    heap_end = keep_end;
}

void heap_get_stats(struct heap_stats *st) {
    struct block *b;
    st->mapped = heap_end - HEAP_START;
    st->used = st->free = st->blocks = st->free_blocks = 0;
    for (b = first; b; b = b->next) {
        st->blocks++;
        if (b->magic == MAGIC_FREE) {
            st->free += b->size;
            st->free_blocks++;
        } else {
            st->used += b->size;
        }
    }
}
