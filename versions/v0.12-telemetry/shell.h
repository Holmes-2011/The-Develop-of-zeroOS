#ifndef SHELL_H
#define SHELL_H

/* 内核命令行：从键盘读一行 → 拆出命令名和参数 → 查命令表执行。
   命令表就是 docs/agent-design.md 里"工具登记表"的第一版：
   以后 Agent 能调用的工具，会从这张表长出来（加上参数格式和权限等级） */

void shell_run(void);   /* 不返回 */

#endif
