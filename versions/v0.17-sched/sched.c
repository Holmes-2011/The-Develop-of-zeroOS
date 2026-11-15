#include "sched.h"
#include "gdt.h"
#include "paging.h"
#include "pmm.h"
#include "console.h"
#include "keyboard.h"
#include "cpu.h"
#include "usermode.h"
#include "idt.h"
#include "kstring.h"

#define KSTACK_SIZE 8192

static struct task tasks[MAX_TASKS];
static unsigned char kstacks[MAX_TASKS][KSTACK_SIZE] __attribute__((aligned(16)));
static unsigned char default_fpu[512] __attribute__((aligned(16)));
static int current;          /* 0 = shell，1.. = 用户任务 */
static int quantum;

extern void switch_to(unsigned int *save_esp, unsigned int new_esp);
extern void user_enter(unsigned int entry, unsigned int stack) __attribute__((noreturn));

static int has_fxsr(void) {
    return cpu_get_info()->has_fxsr;
}

static void fpu_save(void *p)  { __asm__ __volatile__("fxsave (%0)"  : : "r"(p) : "memory"); }
static void fpu_load(const void *p) { __asm__ __volatile__("fxrstor (%0)" : : "r"(p) : "memory"); }

static void task_start(void);
static void copy_name(char *dst, const char *src, int cap);

/* ---- 用户页 ---- */

static int map_user(unsigned int va, unsigned int pages) {
    unsigned int i;
    for (i = 0; i < pages; i++) {
        unsigned int pa = pmm_alloc_page();
        if (!pa) return -1;
        if (paging_map(va + i * 4096, pa, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            pmm_free_page(pa);
            return -1;
        }
    }
    return 0;
}

static void unmap_user(unsigned int va, unsigned int pages) {
    unsigned int i;
    for (i = 0; i < pages; i++) {
        unsigned int pa = paging_translate(va + i * 4096);
        if (pa != 0xFFFFFFFFU) {
            paging_unmap(va + i * 4096);
            pmm_free_page(pa);
        }
    }
}

/* ---- 初始化 ---- */

void sched_init(void) {
    int i;
    memset(&tasks, 0, sizeof(tasks));
    for (i = 0; i < MAX_TASKS; i++) tasks[i].state = ST_FREE;
    current = 0;
    tasks[0].state = ST_RUNNING;
    copy_name(tasks[0].name, "shell", sizeof(tasks[0].name));

    if (has_fxsr()) {
        memset(default_fpu, 0, sizeof(default_fpu));
        *(unsigned int *) (default_fpu + 24) = 0x1F80;   /* MXCSR 默认值 */
    }
}

static void copy_name(char *dst, const char *src, int cap) {
    int i = 0;
    while (i + 1 < cap && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ---- 创建任务 ---- */

int sched_spawn(const unsigned char *img, unsigned int len, const char *name) {
    int id;
    unsigned int base, sp_top, code_pages, stack_pages, i;
    unsigned int *sp;

    for (id = 1; id < MAX_TASKS; id++) {
        if (tasks[id].state == ST_FREE) break;
    }
    if (id == MAX_TASKS) {
        console_write("no free task slot\n");
        return -1;
    }
    if (len == 0 || len > 256 * 1024) {
        console_write("bad program image\n");
        return -1;
    }

    /* 每个任务 16MB 的虚拟窗口，互不重叠：4MB 代码、8MB 栈顶 */
    base        = USER_CODE_BASE + (unsigned int) id * 0x01000000U;
    sp_top      = base + 0x00800000U;
    code_pages  = (len + 4095) / 4096;
    stack_pages = 1;

    if (map_user(base, code_pages) != 0 || map_user(sp_top - stack_pages * 4096, stack_pages) != 0) {
        console_write("out of memory for user pages\n");
        unmap_user(base, code_pages);
        return -1;
    }
    for (i = 0; i < len; i++) *(volatile unsigned char *) (base + i) = img[i];

    tasks[id].state       = ST_READY;
    copy_name(tasks[id].name, name, sizeof(tasks[id].name));
    tasks[id].entry       = base;
    tasks[id].ustack      = sp_top;
    tasks[id].code_base   = base;
    tasks[id].code_pages  = code_pages;
    tasks[id].stack_base  = sp_top - stack_pages * 4096;
    tasks[id].stack_pages = stack_pages;
    tasks[id].exit_code   = 0;
    tasks[id].fault_no    = 0;
    tasks[id].fpu_valid   = 0;
    tasks[id].kstack_top  = (unsigned int) (kstacks[id] + KSTACK_SIZE);

    /* 造一个 switch_to 能"返回"进去的假栈帧：edi esi ebx ebp ret */
    sp = (unsigned int *) tasks[id].kstack_top;
    *--sp = (unsigned int) task_start;
    *--sp = 0;    /* ebp */
    *--sp = 0;    /* ebx */
    *--sp = 0;    /* esi */
    *--sp = 0;    /* edi */
    tasks[id].esp = (unsigned int) sp;

    return id;
}

/* 第一次被调度到时，switch_to 会 ret 到这里 */
static void task_start(void) {
    struct task *t = &tasks[current];
    user_enter(t->entry, t->ustack);     /* 不返回 */
    for (;;) __asm__ __volatile__("cli; hlt");
}

/* ---- 调度 ---- */

static int pick_next(void) {
    int i;
    /* 键盘缓冲区里有东西 → 优先回 shell，保证命令行不卡 */
    if (keyboard_pending() && current != 0) return 0;
    for (i = 1; i < MAX_TASKS; i++) {
        int id = (current + i) % MAX_TASKS;
        if (id == 0) continue;
        if (tasks[id].state == ST_READY) return id;
    }
    if (current != 0) return 0;        /* 没有别的任务 → 回 shell */
    return -1;
}

static void sched_switch(int next) {
    struct task *old = &tasks[current];
    int prev = current;

    if (prev == next) return;

    if (has_fxsr() && prev != 0) {
        fpu_save(old->fpu);
        old->fpu_valid = 1;
    }
    if (old->state == ST_RUNNING) old->state = ST_READY;

    current = next;
    tasks[next].state = ST_RUNNING;
    if (next != 0) tss_set_esp0(tasks[next].kstack_top);   /* 0 号是 shell，跑在内核里，用不到 esp0 */

    switch_to(&old->esp, tasks[next].esp);

    /* 从这里起是"别的任务切回我们"：恢复当前任务的浮点状态 */
    if (has_fxsr()) {
        struct task *me = &tasks[current];
        if (me->fpu_valid) fpu_load(me->fpu);
        else if (current != 0) { fpu_load(default_fpu); me->fpu_valid = 1; }
    }
}

void sched_tick(void) {
    int next;

    if (current == 0) return;           /* 内核（shell）不抢占 */
    if (--quantum > 0) return;

    next = pick_next();
    if (next < 0) next = 0;
    quantum = TASK_QUANTUM;
    sched_switch(next);
}

int sched_idle(void) {
    int next;
    if (current != 0) return 0;
    sched_reap();
    next = pick_next();
    if (next < 0) return 0;
    tasks[0].state = ST_READY;
    quantum = TASK_QUANTUM;
    sched_switch(next);
    return 1;
}

void sched_exit(int code, int fault_no) {
    struct task *t = &tasks[current];
    int next;

    t->exit_code = code;
    t->fault_no  = fault_no;
    t->state     = ST_ZOMBIE;

    next = pick_next();
    if (next < 0) next = 0;
    sched_switch(next);                 /* 僵尸任务不会再被调度，这里不会返回 */
    for (;;) __asm__ __volatile__("cli; hlt");
}

int sched_kill(int id) {
    if (id <= 0 || id >= MAX_TASKS) return 0;
    if (tasks[id].state == ST_FREE || tasks[id].state == ST_ZOMBIE) return 0;
    tasks[id].exit_code = -2;           /* 被 kill */
    tasks[id].fault_no  = 0;
    tasks[id].state     = ST_ZOMBIE;
    return 1;
}

void sched_reap(void) {
    int id;
    if (current != 0) return;           /* 只在 shell 里回收，避免释放正在跑的栈 */
    for (id = 1; id < MAX_TASKS; id++) {
        if (tasks[id].state != ST_ZOMBIE) continue;
        unmap_user(tasks[id].code_base, tasks[id].code_pages);
        unmap_user(tasks[id].stack_base, tasks[id].stack_pages);
        console_write("[task ");
        console_write_dec((unsigned int) id);
        console_write("] ");
        console_write(tasks[id].name);
        if (tasks[id].fault_no) {
            console_write(" killed: ");
            console_write(exception_message((unsigned int) tasks[id].fault_no));
        } else if (tasks[id].exit_code == -2) {
            console_write(" killed (kill command)");
        } else {
            console_write(" exited, code ");
            console_write_dec((unsigned int) tasks[id].exit_code);
        }
        console_write("\n");
        tasks[id].state = ST_FREE;
    }
}

int sched_count(void) {
    int i, n = 0;
    for (i = 1; i < MAX_TASKS; i++) if (tasks[i].state != ST_FREE) n++;
    return n;
}

const struct task *sched_task(int id) {
    if (id < 0 || id >= MAX_TASKS) return 0;
    return &tasks[id];
}

int sched_current(void) {
    return current;
}
