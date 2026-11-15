#include "usermode.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "pmm.h"
#include "console.h"
#include "keyboard.h"
#include "time.h"
#include "tools.h"
#include "kstring.h"

/* 系统调用号 */
enum {
    SYS_EXIT   = 1,
    SYS_WRITE  = 2,
    SYS_GETKEY = 3,
    SYS_UPTIME = 4,
    SYS_TOOL   = 5,
    SYS_SLEEP  = 6
};

#define USER_CODE_MAX   (256 * 1024)     /* 用户程序最大 256KB，够演示和以后的玩具程序 */
#define USER_STACK_SIZE 4096             /* 一页栈，够用；以后做多任务再按程序分配 */

/* 从 ring 3 进来时 CPU 用的内核栈。不能借主循环的栈：主循环的栈帧还在下面活着，
   在它顶上压 iret 现场会把它覆盖掉 */
static unsigned char syscall_stack[8192] __attribute__((aligned(16)));

/* usermode_asm.asm 里保存 / 恢复内核现场用的两个钩子 */
unsigned int kernel_resume_esp;
/* 注意：不能标 noreturn。user_enter 本身不 ret，但 user_resume 恢复现场后
   "返回"的正是 call 指令的下一条——标了 noreturn，编译器会把下一条换成 int3 */
extern void user_enter(unsigned int entry, unsigned int stack);
extern void user_resume(void) __attribute__((noreturn));

/* userbin.asm 用 incbin 把两个程序编进内核只读区 */
extern const unsigned char userprog_start[],  userprog_end[];
extern const unsigned char userfault_start[], userfault_end[];

static volatile int active;
static int          exit_code;
static int          fault_no;
static unsigned int fault_eip;
static unsigned int code_pages, stack_pages;

/* ---- 用户内存 ---- */

static int map_pages(unsigned int va, unsigned int pages) {
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

static void unmap_pages(unsigned int va, unsigned int pages) {
    unsigned int i;
    for (i = 0; i < pages; i++) {
        unsigned int pa = paging_translate(va + i * 4096);
        if (pa != 0xFFFFFFFFU) {
            paging_unmap(va + i * 4096);
            pmm_free_page(pa);
        }
    }
}

/* 用户指针校验：必须在用户地址范围内，且每一页都真的映射给了 ring 3 */
static int user_ok(unsigned int p, unsigned int n) {
    unsigned int a;
    if (n == 0) return 1;
    if (p < USER_CODE_BASE || p >= USER_STACK_TOP) return 0;
    if (p + n < p || p + n > USER_STACK_TOP) return 0;
    for (a = p & ~0xFFFU; a < p + n; a += 4096) {
        if (!paging_user_mapped(a)) return 0;
    }
    return 1;
}

/* 从用户内存拷一个以 0 结尾的字符串到内核缓冲，长度不够就截断 */
static void copy_user_str(char *dst, unsigned int dst_cap, unsigned int src) {
    unsigned int i = 0;
    while (i + 1 < dst_cap) {
        if (!user_ok(src + i, 1)) break;
        dst[i] = (char) *(volatile unsigned char *) (src + i);
        if (!dst[i]) return;
        i++;
    }
    dst[i] = '\0';
}

/* ---- 系统调用 ---- */

void usermode_syscall(struct registers *regs) {
    unsigned int n = regs->eax;
    unsigned int i;

    switch (n) {
    case SYS_EXIT:
        exit_code = (int) regs->ebx;
        active = 0;
        user_resume();

    case SYS_WRITE: {
        unsigned int p = regs->ebx, len = regs->ecx;
        if (len > 4096 || !user_ok(p, len)) { regs->eax = 0xFFFFFFFFU; break; }
        for (i = 0; i < len; i++) console_putc((char) *(volatile unsigned char *) (p + i));
        regs->eax = len;
        break;
    }

    case SYS_GETKEY: {
        int c = keyboard_getchar();
        regs->eax = (c < 0) ? 0xFFFFFFFFU : (unsigned int) c;
        break;
    }

    case SYS_UPTIME:
        regs->eax = time_ms();
        break;

    case SYS_TOOL: {
        unsigned int np = regs->ebx, ap = regs->ecx, op = regs->edx, cap = regs->esi;
        char name[32], args[128], out[TOOL_OUT_MAX];
        enum tool_outcome r;
        unsigned int n_copy, len;

        if (cap > 255) cap = 255;                  /* 让输出 + 结尾 0 一定能塞进 out[] */
        if (!user_ok(np, 1) || !user_ok(ap, 1) || !user_ok(op, cap)) {
            regs->eax = 0xFFFFFFFFU;
            break;
        }
        copy_user_str(name, sizeof(name), np);
        copy_user_str(args, sizeof(args), ap);

        r = tools_invoke(name, args, "user", out);
        len = strlen(out);
        n_copy = (len < cap) ? len : cap;
        for (i = 0; i < n_copy; i++) *(volatile char *) (op + i) = out[i];
        if (cap > n_copy) *(volatile char *) (op + n_copy) = '\0';
        regs->eax = (unsigned int) r;
        break;
    }

    case SYS_SLEEP: {
        unsigned int start = time_ms();
        /* 系统调用是从中断门进来的，IF 已经被 CPU 清掉；不重新开中断的话 hlt 永远醒不来。
           sti; hlt; cli 三条连在一起，醒来后立刻把 IF 关回去，保持处理程序里的约定 */
        while (time_ms() - start < regs->ebx) {
            __asm__ __volatile__("sti; hlt; cli");
        }
        regs->eax = 0;
        break;
    }

    default:
        regs->eax = 0xFFFFFFFFU;                   /* 未知调用号 */
        break;
    }
}

void usermode_fault(struct registers *regs) {
    fault_no  = (int) regs->int_no;
    fault_eip = regs->eip;
    active    = 0;
    user_resume();
}

int usermode_active(void) {
    return active;
}

/* ---- 跑一个程序 ---- */

int usermode_run(const unsigned char *image, unsigned int len, const char *name) {
    unsigned int i;

    if (active) {
        console_write("a user program is already running\n");
        return -1;
    }
    if (len == 0 || len > USER_CODE_MAX) {
        console_write("bad program image\n");
        return -1;
    }

    code_pages  = (len + 4095) / 4096;
    stack_pages = USER_STACK_SIZE / 4096;

    if (map_pages(USER_CODE_BASE, code_pages) != 0 ||
        map_pages(USER_STACK_TOP - stack_pages * 4096, stack_pages) != 0) {
        console_write("out of memory for user pages\n");
        unmap_pages(USER_CODE_BASE, code_pages);
        return -1;
    }

    for (i = 0; i < len; i++) *(volatile unsigned char *) (USER_CODE_BASE + i) = image[i];

    /* 从 ring 3 进来的中断 / 系统调用都用这块内核栈 */
    tss_set_esp0((unsigned int) (syscall_stack + sizeof(syscall_stack)));

    exit_code = -1;
    fault_no  = 0;
    fault_eip = 0;
    active    = 1;

    console_write("[user] ");
    console_write(name);
    console_write(": entering ring 3\n");

    user_enter(USER_CODE_BASE, USER_STACK_TOP);   /* 正常不返回；从 user_resume 回到这里 */

    active = 0;
    if (fault_no) {
        console_set_color(VGA_LIGHT_RED);
        console_write("[user] program killed: ");
        console_write(exception_message((unsigned int) fault_no));
        console_write(" at EIP ");
        console_write_hex(fault_eip);
        console_set_color(VGA_LIGHT_GREY);
        console_write("\n");
    } else {
        console_write("[user] exit code ");
        console_write_dec((unsigned int) exit_code);
        console_write("\n");
    }

    unmap_pages(USER_CODE_BASE, code_pages);
    unmap_pages(USER_STACK_TOP - stack_pages * 4096, stack_pages);

    return fault_no ? -1 : exit_code;
}
