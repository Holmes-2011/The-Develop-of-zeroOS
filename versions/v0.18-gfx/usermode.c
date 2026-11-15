#include "usermode.h"
#include "sched.h"
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

/* 用户指针校验：必须在用户地址范围内，且每一页都真的映射给了 ring 3 */
static int user_ok(unsigned int p, unsigned int n) {
    unsigned int a;
    if (n == 0) return 1;
    if (p < USER_CODE_BASE || p >= USER_CODE_BASE + MAX_TASKS * 0x01000000U) return 0;
    if (p + n < p || p + n > USER_CODE_BASE + MAX_TASKS * 0x01000000U) return 0;
    for (a = p & ~0xFFFU; a < p + n; a += 4096) {
        if (!paging_user_mapped(a)) return 0;
    }
    return 1;
}

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

void usermode_syscall(struct registers *regs) {
    unsigned int n = regs->eax;
    unsigned int i;

    switch (n) {
    case SYS_EXIT:
        sched_exit((int) regs->ebx, 0);        /* 不返回 */

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

        if (cap > 255) cap = 255;
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
        while (time_ms() - start < regs->ebx) {
            __asm__ __volatile__("sti; hlt; cli");
        }
        regs->eax = 0;
        break;
    }

    default:
        regs->eax = 0xFFFFFFFFU;
        break;
    }
}

void usermode_fault(struct registers *regs) {
    sched_exit(-1, (int) regs->int_no);        /* 杀掉这个任务，回 shell */
}

int usermode_active(void) {
    return sched_current() != 0;
}
