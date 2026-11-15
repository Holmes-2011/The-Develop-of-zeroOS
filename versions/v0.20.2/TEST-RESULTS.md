# v0.20.2 测试记录

## 本版内容

`AUTO_DESKTOP` 编译开关：`make AUTO_DESKTOP=1` 的镜像开机直接进桌面；默认 `0` 仍从命令行开始（自动化测试用）。

## 测试结果

| 测试项 | 结果 |
|---|---|
| 默认构建（AUTO_DESKTOP=0）全套 | ✅ **163/163** |
| AUTO_DESKTOP=1 启动 | ✅ 串口出现 `desktop autostart`，屏幕直接是桌面 |
| 桌面 → ESC → shell | ✅ `desktop: closed`，随后 `echo ok` 正常 |
| 桌面 → 任务栏 Exit（鼠标） | ✅ 回到 shell（v0.20.1 已验证的同一路径） |
| 60 秒空闲退出 | 代码路径（未跑满 60 秒） |

### 验证方法

```
make AUTO_DESKTOP=1
qemu ... -drive format=raw,file=build/os-image.bin
# 4 秒后 screendump：像素 (300,200) = 窗口正文 (236,238,242)
# sendkey esc → 串口出现 desktop: closed；输入 echo ok 正常
```

## 已知限制

- 默认 `make` 不会自启桌面（保证 CI 和命令行流程不被桌面挡住）。
- 桌面退出后不会自动再进；需要手动 `desktop`。
- 浏览器：见 release notes 的“做不到”说明——缺网卡驱动、TCP/IP、TLS、文件系统、渲染引擎。

## 截图

[`docs/images/v0.20.2-autodesktop.png`](../../docs/images/v0.20.2-autodesktop.png)
