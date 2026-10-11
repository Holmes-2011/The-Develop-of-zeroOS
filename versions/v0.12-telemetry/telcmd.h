#ifndef TELCMD_H
#define TELCMD_H

/* 感知层的命令行命令（v0.12） */
void cmd_metrics(const char *args);    /* 各计数器的累计值和最近 1 秒的速率、CPU 忙碌比例 */
void cmd_events(const char *args);     /* 事件日志 */
void cmd_telstream(const char *args);  /* telstream on|off：每次采样往串口输出一行（采集训练数据用） */

#endif
