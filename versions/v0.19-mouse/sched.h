#ifndef SCHED_H
#define SCHED_H

/* 调度器（v0.17）：抢占式轮转，任务都是 ring 3 程序；shell 是特殊的 0 号任务，
   只在空闲（等键盘）时让出 CPU。内核代码本身不抢占——只有用户态会被时钟打断。

   上下文切换用 switch_to：保存/恢复内核栈指针和 callee-saved 寄存器。
   每个任务有自己的 8KB 内核栈，TSS.esp0 跟着当前任务走。
   浮点 / SSE 状态用 FXSAVE / FXRSTOR 存到任务的 fpu 区（没有 fxsr 的 CPU 跳过）。 */

#define MAX_TASKS     6
#define TASK_QUANTUM  3          /* 每次调度给几个时钟 tick（10ms 一个） */

enum task_state {
    ST_FREE = 0,
    ST_READY,
    ST_RUNNING,
    ST_ZOMBIE
};

struct task {
    int            state;
    char           name[12];
    unsigned int   esp;              /* switch_to 保存点 */
    unsigned int   kstack_top;       /* TSS.esp0 */
    unsigned int   entry;            /* 用户态入口 */
    unsigned int   ustack;           /* 用户栈顶 */
    unsigned int   code_base;
    unsigned int   code_pages;
    unsigned int   stack_base;
    unsigned int   stack_pages;
    int            exit_code;        /* 正常退出码；被杀为 -1 */
    int            fault_no;         /* 被杀时的异常号（0 = 正常） */
    int            fpu_valid;
    unsigned char  fpu[512] __attribute__((aligned(16)));
};

void sched_init(void);                                  /* 登记 shell 任务（0 号） */
int  sched_spawn(const unsigned char *img, unsigned int len, const char *name);
void sched_tick(void);                                  /* 时钟中断里调用（可能切走） */
int  sched_idle(void);                                  /* shell 空闲时调用：让出 CPU；返回 1 = 真的切走了 */
void sched_exit(int code, int fault_no) __attribute__((noreturn)); /* 当前任务结束 */
int  sched_kill(int id);                                /* 杀一个任务；返回是否成功 */
int  sched_count(void);
const struct task *sched_task(int id);
int  sched_current(void);
void sched_reap(void);                                  /* 回收僵尸任务（只能在 shell 上下文调用） */

#endif
