#ifndef USERMODE_H
#define USERMODE_H

/* 用户态（v0.16）：ring 3 程序 + int 0x80 系统调用。

   一个程序就是一段裸二进制，被复制到 USER_CODE_BASE 执行。系统调用号放 EAX，
   参数放 EBX / ECX / EDX / ESI，返回值回 EAX：

     1 exit(code)                     结束程序
     2 write(buf, len)                把用户内存里的字节写到屏幕 + 串口
     3 getkey()                       取一个键盘字符，没有返回 -1
     4 uptime()                       开机毫秒数
     5 tool(name, args, out, cap)     通过内核工具层调用一个工具（caller = "user"）
     6 sleep(ms)                      睡一会儿（期间中断照常处理）

   内核只认这三个地址段：代码段 [USER_CODE_BASE, ...)、栈 [USER_STACK_TOP-4KB, USER_STACK_TOP)。
   系统调用里的用户指针一律先校验（范围 + 页面确实映射且带 USER 位），
   校验不过返回 -1，绝不让内核替用户态去碰非法地址。 */

#define USER_CODE_BASE 0x00400000U
#define USER_STACK_TOP 0x00800000U

struct registers;

void usermode_syscall(struct registers *regs);   /* int 0x80 分发 */
void usermode_fault(struct registers *regs);     /* ring 3 里出异常 → 杀掉程序，回 shell */
int  usermode_active(void);

/* 跑一个内嵌程序。返回退出码；被异常杀掉时返回 -1 */
int  usermode_run(const unsigned char *image, unsigned int len, const char *name);

#endif
