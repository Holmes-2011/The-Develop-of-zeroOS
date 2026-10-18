#ifndef TOOLS_H
#define TOOLS_H

/* 工具层（v0.13）：Agent 能对系统做的每一件事，都是这里登记的一个"工具"。

   不管以后接的是哪个模型，模型只能通过 tools_invoke() 做事；
   tools_invoke() 按工具的权限等级和当前模式决定：直接执行 / 弹确认框 / 拒绝，
   然后把这次调用写进审计日志。命令行的 `tool` 命令走的是同一条路——
   人和 Agent 用同一个入口、同一套规矩。

   权限等级（和 docs/agent-design.md 的表一致）：
     只读      查状态，不改任何东西            默认模式：直接执行   开发者模式：直接执行
     可撤销    改了能改回来（比如开关遥测流）    默认模式：直接执行   开发者模式：直接执行
     破坏性    改了回不来（清日志、以后的删文件）默认模式：要确认     开发者模式：要确认
     系统级    动系统本身（故障注入、以后的装驱动）默认模式：拒绝      开发者模式：要确认

   确认框由内核绘制、只认物理键盘（见 confirm.h）。Agent 申请确认，但画不出"已同意"。 */

enum tool_level { TOOL_READONLY, TOOL_REVERSIBLE, TOOL_DESTRUCTIVE, TOOL_SYSTEM };

enum tool_outcome {
    TOOL_OK,              /* 执行了，成功 */
    TOOL_FAILED,          /* 执行了，工具自己报失败 */
    TOOL_DENIED_POLICY,   /* 当前模式下这个等级不允许 */
    TOOL_DENIED_USER,     /* 确认框里用户按了 n */
    TOOL_TIMEOUT,         /* 确认框没人回答 */
    TOOL_UNKNOWN,         /* 没有这个工具 */
    TOOL_BAD_ARGS         /* 参数不对 */
};

#define TOOL_OUT_MAX 512  /* 工具输出的最大长度 */

struct tool {
    const char     *name;         /* 比如 "get_uptime" */
    const char     *description;  /* 给模型和人看的一句话说明 */
    const char     *params;       /* 参数说明，没有参数就是 "" */
    enum tool_level level;
    /* 执行：把结果写进 out（最多 cap 字节，含结尾 0）。返回 0 成功，-1 失败，-2 参数不对 */
    int (*run)(const char *args, char *out, unsigned int cap);
};

void               tools_init(void);                 /* 登记内置工具 */
void               tools_register(const struct tool *t);
int                tools_count(void);
const struct tool *tools_get(int index);
const struct tool *tools_find(const char *name);
const char        *tool_level_name(enum tool_level level);
const char        *tool_outcome_name(enum tool_outcome o);

/* 调用一个工具。caller 是谁在调（"shell" / "agent"），只用于审计。
   out 至少 TOOL_OUT_MAX 字节：成功时是工具的输出，失败时是原因 */
enum tool_outcome  tools_invoke(const char *name, const char *args, const char *caller, char *out);

/* 开发者模式。打开要过确认框；关闭立即生效 */
int  tools_devmode(void);
int  tools_set_devmode(int on);   /* 返回 1 成功切换，0 被拒绝（确认框按了 n 或超时） */

/* ---- 审计日志：每次调用一条，环形保存最近 AUDIT_RING 条 ---- */
#define AUDIT_RING 64
#define AUDIT_ARGS 24

struct audit_rec {
    unsigned int     t_ms;
    const char      *tool;            /* 登记表里的名字；未知工具时是 "?" */
    const char      *caller;
    enum tool_level  level;
    enum tool_outcome outcome;
    int              devmode;         /* 调用时是否在开发者模式 */
    char             args[AUDIT_ARGS];/* 参数开头的一段 */
};
int          audit_get(struct audit_rec *out, int max);   /* 从旧到新，返回条数 */
unsigned int audit_total(void);                           /* 开机以来一共多少条（含被覆盖的） */

/* ---- 给工具实现用的输出小工具：往 out 里追加，不会越界 ---- */
struct tool_out { char *p; unsigned int cap, len; };
void tout_init(struct tool_out *o, char *buf, unsigned int cap);
void tout_str(struct tool_out *o, const char *s);
void tout_dec(struct tool_out *o, unsigned int n);
void tout_hex(struct tool_out *o, unsigned int n);

#endif
