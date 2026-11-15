#ifndef WM_H
#define WM_H

/* 桌面 / 窗口管理（v0.19）：两个窗口、鼠标指针、点击聚焦、拖标题栏移动。
   进入桌面后独占屏幕，按 ESC 回命令行。 */

void wm_run(void);      /* 进入桌面循环，ESC 退出 */
void wm_info(void);     /* 把窗口位置 / 焦点打印到串口（测试用） */

#endif
