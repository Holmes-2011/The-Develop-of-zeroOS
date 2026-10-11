#ifndef DISKTEST_H
#define DISKTEST_H

/* 硬盘相关的 shell 命令（v0.11）。args 是命令名后面的部分 */
void cmd_disks(const char *args);       /* 列出所有块设备 */
void cmd_diskread(const char *args);    /* diskread <盘> <扇区号>：十六进制显示一个扇区的开头 */
void cmd_diskcheck(const char *args);   /* diskcheck <盘> <起始扇区> <扇区数>：校验测试图案 */
void cmd_diskbench(const char *args);   /* diskbench <盘> <MB>：测读盘速度 */

#endif
