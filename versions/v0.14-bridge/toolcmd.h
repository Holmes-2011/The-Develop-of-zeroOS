#ifndef TOOLCMD_H
#define TOOLCMD_H

/* 工具层的命令行命令（v0.13） */
void cmd_tools(const char *args);     /* 列出所有工具和权限等级 */
void cmd_tool(const char *args);      /* tool <名字> [参数]：通过工具层调用（和 Agent 走同一条路） */
void cmd_devmode(const char *args);   /* devmode [on|off] */
void cmd_audit(const char *args);     /* 审计日志 */

#endif
