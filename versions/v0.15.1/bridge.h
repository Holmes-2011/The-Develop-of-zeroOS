#ifndef BRIDGE_H
#define BRIDGE_H

/* 桥接协议的内核侧（v0.14，规格见 docs/agent-bridge-protocol.md）。

   宿主机（Mac 上的 Agent 程序）通过 COM1 发来一行一条的 JSON；内核回复以 "@" 开头的 JSON 行。
   内核永远不信任串口：它只能请求调用工具（走 tools_invoke，权限、确认框、审计一样不少，
   caller 记为 "agent"）和提交直觉判断（只记录、只显示，不自动做任何事）。
   串口不能打开开发者模式、不能回答确认框——那两样只认物理键盘。

   处理在主循环里做（bridge_poll）：串口中断只把字节放进缓冲区。 */

#define BRIDGE_VERSION  1
#define BRIDGE_LINE_MAX 512   /* 含结尾换行 */

void bridge_init(void);
void bridge_poll(void);        /* 主循环空闲时调用：拼行、解析、执行、回复、推送事件 */
int  bridge_connected(void);   /* 收到过匹配版本的 hello */

/* 最后一次直觉判断（给 get_intuition 工具用）。没有收到过时 class 为空串 */
struct intuition {
    char         cls[24];
    unsigned int confidence;
    char         note[81];
    unsigned int t_ms;
    unsigned int count;        /* 开机以来收到过几条 */
};
const struct intuition *bridge_last_verdict(void);

#endif
