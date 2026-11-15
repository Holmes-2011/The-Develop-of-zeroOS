#!/usr/bin/env python3
"""qmp_mouse.py <qmp.sock> <命令...> —— 用 QMP 给 USB 平板发绝对指针事件

命令：
  move <x> <y>     绝对移动到屏幕坐标（1024x768）
  down / up        左键按下 / 松开
"""
import json, socket, sys, time

def conv(x, y):
    return (x * 32767 // 1023, y * 32767 // 767)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sys.argv[1])
f = sock.makefile('rw')
f.readline()                       # QMP greeting
f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n"); f.flush(); f.readline()

args = sys.argv[2:]
i = 0
while i < len(args):
    cmd = args[i]
    if cmd == "move":
        x, y = int(args[i+1]), int(args[i+2]); i += 3
        ax, ay = conv(x, y)
        ev = {"execute": "input-send-event", "arguments": {"events": [
            {"type": "abs", "data": {"axis": "x", "value": ax}},
            {"type": "abs", "data": {"axis": "y", "value": ay}}]}}
    elif cmd in ("down", "up"):
        ev = {"execute": "input-send-event", "arguments": {"events": [
            {"type": "btn", "data": {"down": cmd == "down", "button": "left"}}]}}
        i += 1
    else:
        raise SystemExit("bad command: " + cmd)
    f.write(json.dumps(ev) + "\n"); f.flush()
    f.readline()
    time.sleep(0.25)                     # 让固件把这一帧报上来，再发下一条
print("ok")
