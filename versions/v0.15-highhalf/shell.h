#ifndef SHELL_H
#define SHELL_H

/* 内核命令行：从键盘读一行 → 拆出命令名和参数 → 查命令表执行。
   这张表只是人用的入口；Agent 能调用的工具登记在 tools.c 的表里
   （带参数格式和权限等级），和这里不重复。 */

void shell_run(void);   /* 不返回 */

#endif
