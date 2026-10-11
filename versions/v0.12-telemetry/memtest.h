#ifndef MEMTEST_H
#define MEMTEST_H

/* 内存自检：把物理页分配、分页、内核堆各跑一遍，逐项打印 [ok] / [FAIL]。
   既是给人用的命令（shell 里的 memtest），也是 test-boot.sh 的自动测试入口。
   返回失败的项数 */
int memtest_run(void);

#endif
