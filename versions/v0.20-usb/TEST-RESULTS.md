# v0.20 测试记录

## 本版内容

**USB 平板驱动**：让 UTM / QEMU 的 usb-tablet（绝对指针）能被 zeroOS 使用。

| 改动 | 文件 |
|---|---|
| PCI 配置空间访问、按 class 找 UHCI | `pci.c` / `pci.h` |
| UHCI：复位、帧列表、端口复位/使能、QH/TD 传输 | `usb.c` / `usb.h` |
| 控制传输 + 枚举 + HID 中断 IN 报告解析 | `usb.c` |
| 桌面优先用绝对指针，PS/2 作回退 | `wm.c` |
| 第八阶段：QMP 注入绝对坐标拖动窗口 | `test-boot.sh`、`qmp_mouse.py` |

### 踩到的坑

UHCI 的 TD "Maximum Length" 字段是 **长度 − 1**；零长度要写 `0x7FF`（+1 后溢出回 0）。
一开始直接写 8，控制器会发 9 个字节的 SETUP，设备直接 STALL，表现就是所有控制传输超时。

## 测试结果

| 测试项 | 结果 |
|---|---|
| `-M pc` 全套 | ✅ **163/163** |
| `-M q35` | ✅ 155/155（USB 阶段跳过：只有 PIIX3 UHCI） |
| USB：`usb: tablet ready` | ✅ |
| USB：绝对坐标拖动后 `wm: win0=180,140` | ✅ |
| USB：帧缓冲标题栏像素 `(450,150)=(51,102,204)` | ✅ |

### 测试方法

```
QEMU ... -usb -device usb-tablet,bus=usb-bus.0 -qmp unix:sock
python3 qmp_mouse.py sock move 200 90 down move 300 150 up
```

`qmp_mouse.py` 把屏幕坐标换算成 0..32767 的绝对值，经 QMP `input-send-event` 发给平板。

## 已知限制

- 只有 UHCI；xHCI / EHCI 没做。
- 只有 HID 平板一个 class；键盘 / U 盘 / hub 都没有。
- 报告格式按 QEMU usb-tablet；其他平板可能要读 HID 报告描述符。

## 截图

[`docs/images/v0.20-usb-mouse.png`](../../docs/images/v0.20-usb-mouse.png)
