# v0.19 测试记录

## 本版内容

**桌面 + 鼠标**：PS/2 鼠标（IRQ12）驱动指针；`desktop` 命令进入两个窗口的桌面，
点标题栏拖动、点击置顶、`ESC` 回命令行；开机默认画深色渐变背景。

| 改动 | 文件 |
|---|---|
| PS/2 鼠标初始化（8042 aux、IRQ12）与 3 字节包解析 | `mouse.c` / `mouse.h` |
| 桌面：窗口绘制、指针（保存/还原背景）、命中测试、拖动、焦点 | `wm.c` / `wm.h` |
| 任意位置画字、实心矩形、读像素等图形辅助 | `gfx.c` / `gfx.h` |
| 开机背景 | `gfx.c`、`kernel.c` |
| `desktop` / `wminfo` 命令 | `shell.c` |
| 第七阶段：mouse_move / mouse_button / screendump / wminfo | `test-boot.sh` |

## 测试结果

| 测试项 | 结果 |
|---|---|
| `-M pc` 全套 | ✅ **160/160** |
| `-M q35` | 见下方 |
| 桌面：进入 / ESC 退出后命令可用 | ✅ |
| 桌面：拖动后 `wm: win0=180,140` | ✅ |
| 桌面：帧缓冲标题栏像素 `(450,150)=(51,102,204)` | ✅ |

### 测试方法

用 QEMU monitor 注入鼠标事件：

```
mouse_move -312 -294      # 指针从屏幕中心挪到 win0 标题栏
mouse_button 1            # 按下 → 开始拖
mouse_move 100 60         # 移动 (+100,+60)
mouse_button 0            # 松开
wminfo                    # 串口打印：wm: win0=180,140 win1=560,160 focus=0
```

再用 `screendump` 抓帧缓冲，`check_ppm.py` 校验标题栏确实跟着移动。

## 已知限制

| 项目 | 说明 |
|---|---|
| 鼠标 | 只有 PS/2；USB 平板/鼠标还不支持（UTM 默认还接了一个 usb-tablet，用不上） |
| 窗口 | 固定两个、不能关闭/缩放；没有合成器，拖动只重绘涉及的区域 |
| 输入 | 桌面里键盘只响应 ESC，其他按键丢弃 |
| 光标 | 8×12 单色箭头，没有主题/缩放 |
| 测试 | 像素断言只覆盖标题栏一个点；窗口内容靠截图人工看 |

## 截图

[`docs/images/v0.19-desktop.png`](../../docs/images/v0.19-desktop.png)

## 串口输出

见 [`serial-output.txt`](serial-output.txt) 和 [`bridge-test.log`](bridge-test.log)。
