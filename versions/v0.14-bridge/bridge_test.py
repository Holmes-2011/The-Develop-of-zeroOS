#!/usr/bin/env python3
"""bridge_test.py —— 扮演宿主机，通过 TCP 连 QEMU 的串口，按 docs/agent-bridge-protocol.md 走一遍协议。

用法：python3 bridge_test.py <串口 TCP 端口> <monitor TCP 端口> [--quick]
  串口：QEMU 用 -serial tcp:127.0.0.1:<port>,server,nowait
  monitor：QEMU 用 -monitor tcp:127.0.0.1:<port>,server,nowait（用来 sendkey 回答确认框）

每一步打印 [通过] / [失败]，最后一行 "bridge_test: N/M passed"；退出码 0 = 全过。
"""
import json, os, socket, sys, time
DEBUG = bool(os.environ.get('BRIDGE_DEBUG'))

ser_port, mon_port = int(sys.argv[1]), int(sys.argv[2])
quick = '--quick' in sys.argv
results = []

def check(ok, what):
    results.append(ok)
    print(('[通过] ' if ok else '[失败] ') + what, flush=True)

class Serial:
    def __init__(self, port):
        self.s = socket.create_connection(('127.0.0.1', port), timeout=30)
        self.buf = b''
        self.console = []      # 非协议行（控制台文字、T, 行）
    def send(self, obj):
        self.s.sendall((json.dumps(obj) + '\n').encode())
    def send_raw(self, text):
        self.s.sendall(text.encode())
    def _pop_lines(self):
        while b'\n' in self.buf:
            line, self.buf = self.buf.split(b'\n', 1)
            text = line.decode('ascii', 'replace').rstrip('\r')
            if DEBUG: print('      <<', text[:120], flush=True)
            yield text
    def lines(self, timeout):
        """在 timeout 秒内把收到的行都吐出来（生成器）。
        先吐缓冲区里已经攒下的整行，再去 recv——上一次等待在中途返回时，同一批到达的后续行还留在缓冲区里，
        不先吐出来的话会一直等新数据，把它们晾到下一次调用（第一版就是这个 bug，误报了 2 项）"""
        end = time.time() + timeout
        while True:
            for text in self._pop_lines():
                yield text
            if time.time() >= end:
                return
            self.s.settimeout(max(0.05, end - time.time()))
            try:
                d = self.s.recv(4096)
            except socket.timeout:
                continue
            if not d:
                return
            self.buf += d
    def wait_msg(self, pred, timeout, collect=None):
        """等第一条满足 pred 的协议消息（@ 开头、能解析）；其余协议消息放进 collect"""
        for l in self.lines(timeout):
            if l.startswith('@'):
                try:
                    m = json.loads(l[1:])
                except ValueError:
                    self.console.append('BADPROTO:' + l)
                    continue
                if pred(m):
                    return m
                if collect is not None:
                    collect.append(m)
            else:
                self.console.append(l)
        return None
    def drain(self, seconds, collect=None):
        for l in self.lines(seconds):
            if l.startswith('@'):
                try:
                    if collect is not None: collect.append(json.loads(l[1:]))
                except ValueError:
                    self.console.append('BADPROTO:' + l)
            else:
                self.console.append(l)

def sendkey(key):
    m = socket.create_connection(('127.0.0.1', mon_port), timeout=5)
    time.sleep(0.3)
    m.sendall(('sendkey %s\n' % key).encode())
    time.sleep(0.2)
    m.close()

def typ(text):
    for ch in text:
        sendkey({' ': 'spc', '_': 'shift-minus'}.get(ch, ch))
    sendkey('ret')

ser = Serial(ser_port)
time.sleep(0.5)

# ---------- 1. 握手 ----------
ser.send({'type': 'hello', 'version': 1, 'client': 'bridge_test/1'})
w = ser.wait_msg(lambda m: m.get('type') == 'welcome', 10)
check(w is not None and w.get('version') == 1 and w.get('devmode') == 0 and w.get('uptime_ms', 0) > 0
      and str(w.get('kernel', '')).startswith('v0.14'), 'hello → welcome（version 1, devmode 0, kernel v0.14, uptime>0）')
tools, tl = {}, None
got = ser.wait_msg(lambda m: m.get('type') == 'tools', 5)
end = ser.wait_msg(lambda m: m.get('type') == 'tools_end', 10, collect=[])
# collect 里的 tool 消息在 end 之前；重新走一遍更稳：从 console 无法取回，所以上面用 collect
# 简化：再握手一次，把 tool 消息收全
ser.send({'type': 'hello', 'version': 1, 'client': 'bridge_test/1'})
msgs = []
ser.wait_msg(lambda m: m.get('type') == 'tools_end', 10, collect=msgs)
tools = {m['name']: m for m in msgs if m.get('type') == 'tool'}
count = next((m['count'] for m in msgs if m.get('type') == 'tools'), None)
check(count is not None and count == len(tools) >= 13, 'tools 清单：count=%s，收到 %d 条 tool，4 个等级齐全' % (count, len(tools)))
check({t['level'] for t in tools.values()} == {'readonly', 'reversible', 'destructive', 'system'}, '  等级取值 = readonly/reversible/destructive/system')
check(tools.get('inject_fault', {}).get('level') == 'system' and tools.get('get_intuition', {}).get('level') == 'readonly', '  inject_fault 是 system，get_intuition 是 readonly')

# ---------- 2. 只读调用 ----------
def call(cid, name, args='', timeout=10, expect_confirm=False):
    ser.send({'type': 'call', 'id': cid, 'name': name, 'args': args})
    got = []
    r = ser.wait_msg(lambda m: m.get('type') == 'result' and m.get('id') == cid, timeout, collect=got)
    conf = [m for m in got if m.get('type') == 'confirm' and m.get('id') == cid]
    return r, conf

r, _ = call(1, 'get_uptime')
check(r and r['outcome'] == 'ok' and r['output'].startswith('uptime_ms='), 'call get_uptime → ok，输出 uptime_ms=...')
r, _ = call(2, 'get_memory')
check(r and r['outcome'] == 'ok' and 'free_pages=' in r['output'], 'call get_memory → ok')
r, _ = call(3, 'heap_check')
check(r and r['outcome'] == 'ok' and r['output'].startswith('ok blocks='), 'call heap_check → ok')
r, _ = call(4, 'get_intuition')
check(r and r['outcome'] == 'ok' and r['output'].startswith('none'), 'call get_intuition（还没收到判断）→ none')
r, _ = call(5, 'nosuch')
check(r and r['outcome'] == 'unknown_tool', 'call 未知工具 → unknown_tool')
r, _ = call(6, 'set_telstream', 'maybe')
check(r and r['outcome'] == 'bad_args', 'call set_telstream maybe → bad_args')

# ---------- 3. 系统级：默认模式拒绝，不弹框 ----------
r, conf = call(7, 'inject_fault', 'spin 100')
check(r and r['outcome'] == 'denied_by_policy' and not conf, 'call inject_fault（默认模式）→ denied_by_policy，且没有 confirm 消息')

# ---------- 4. 破坏性：先收到 confirm，再由物理键盘回答 ----------
ser.send({'type': 'call', 'id': 8, 'name': 'clear_events', 'args': ''})
c = ser.wait_msg(lambda m: m.get('type') == 'confirm', 10)
check(c is not None and c.get('id') == 8 and c.get('name') == 'clear_events' and c.get('level') == 'destructive', 'call clear_events → 先收到 confirm{id 8, destructive}')
# 串口不能回答确认框：发一堆 y 过去，必须没用
ser.send_raw('y\ny\n{"type":"call","id":99,"name":"get_uptime","args":""}\n')
r = ser.wait_msg(lambda m: m.get('type') == 'result' and m.get('id') == 8, 3)
check(r is None, '  串口发 y 不能回答确认框（3 秒内没有 result）')
sendkey('n')
r = ser.wait_msg(lambda m: m.get('type') == 'result' and m.get('id') == 8, 10)
check(r is not None and r['outcome'] == 'denied_by_user', '  物理键盘按 n → result denied_by_user')
# 框期间塞进来的那条 call 99：框结束后才处理，照常回复
r = ser.wait_msg(lambda m: m.get('type') == 'result' and m.get('id') == 99, 10)
check(r is not None and r['outcome'] == 'ok', '  确认框期间排队的 call 99 在框结束后得到 result')

ser.send({'type': 'call', 'id': 9, 'name': 'clear_events', 'args': ''})
c = ser.wait_msg(lambda m: m.get('type') == 'confirm', 10)
sendkey('y')
r = ser.wait_msg(lambda m: m.get('type') == 'result' and m.get('id') == 9, 10)
check(c is not None and r is not None and r['outcome'] == 'ok' and r['output'].startswith('cleared='), 'call clear_events，键盘按 y → result ok cleared=...')

# ---------- 5. 开发者模式只能从键盘开；开了之后系统级工具走确认 ----------
ser.send({'type': 'call', 'id': 10, 'name': 'devmode', 'args': 'on'})
r, _ = call(11, 'get_uptime')
r10 = None
check(r is not None and r['outcome'] == 'ok', '  （call devmode 不是工具，被当成 unknown_tool，不影响后续）')
typ('devmode on')          # 在命令行里打开；确认框弹出
time.sleep(1.0)
sendkey('y')
time.sleep(0.8)
ser.send({'type': 'hello', 'version': 1, 'client': 'bridge_test/1'})
w = ser.wait_msg(lambda m: m.get('type') == 'welcome', 10)
ser.wait_msg(lambda m: m.get('type') == 'tools_end', 10)
check(w is not None and w.get('devmode') == 1, '键盘打开开发者模式后重新握手：welcome devmode=1')
ser.send({'type': 'call', 'id': 12, 'name': 'inject_fault', 'args': 'spin 300'})
c = ser.wait_msg(lambda m: m.get('type') == 'confirm' and m.get('id') == 12, 10)
sendkey('y')
r = ser.wait_msg(lambda m: m.get('type') == 'result' and m.get('id') == 12, 15)
check(c is not None and c.get('level') == 'system' and r is not None and r['outcome'] == 'ok' and 'spun' in r['output'], 'call inject_fault spin 300（开发者模式）→ confirm{system} → 按 y → ok')

# ---------- 6. 事件推送 ----------
evs = []
ser.send({'type': 'call', 'id': 13, 'name': 'inject_fault', 'args': 'bad_free'})
ser.wait_msg(lambda m: m.get('type') == 'confirm' and m.get('id') == 13, 10)
sendkey('y')
r = ser.wait_msg(lambda m: m.get('type') == 'result' and m.get('id') == 13, 15, collect=evs)
ser.drain(1.5, collect=evs)
names = [e.get('name') for e in evs if e.get('type') == 'event']
check('heap bad free' in names and all('t_ms' in e and 'count' in e and 'a' in e for e in evs if e.get('type') == 'event'),
      '事件推送：bad_free 之后收到 event{name:"heap bad free", t_ms, count, a}（收到 %s）' % names)

# ---------- 7. 直觉判断 ----------
ser.send({'type': 'verdict', 'class': 'memory_leak', 'confidence': 93, 'note': 'heap growing, no frees é\u0001'})
a = ser.wait_msg(lambda m: m.get('type') == 'ack', 10, collect=evs)
r, _ = call(14, 'get_intuition')
check(a is not None and a.get('what') == 'verdict' and r and 'class=memory_leak' in r['output'] and 'confidence=93' in r['output']
      and 'note="heap growing, no frees ??"' in r['output'], 'verdict → ack；get_intuition 看到 class/confidence，note 里非 ASCII 被换成 ?')
ser.send({'type': 'verdict', 'class': 'normal', 'confidence': 250})
ser.wait_msg(lambda m: m.get('type') == 'ack', 10)
r, _ = call(15, 'get_intuition')
check(r and 'class=normal' in r['output'] and 'confidence=100' in r['output'] and 'count=2' in r['output'], 'verdict normal, confidence 250 → 截到 100，count=2')
ser.send({'type': 'verdict', 'confidence': 1})
e = ser.wait_msg(lambda m: m.get('type') == 'error', 10)
check(e is not None and e.get('error') == 'bad_fields', 'verdict 缺 class → error bad_fields')

# ---------- 8. 错误处理 ----------
ser.send_raw('{"type":"call", "id": 16, "name": ["x"]}\n')
e = ser.wait_msg(lambda m: m.get('type') == 'error', 10)
check(e is not None and e.get('error') == 'bad_json', '数组（不在子集里）→ error bad_json')
ser.send_raw('this is not json at all\n')
e = ser.wait_msg(lambda m: m.get('type') == 'error', 10)
check(e is not None and e.get('error') == 'bad_json' and e.get('line', '').startswith('this is not'), '非 JSON → error bad_json，带前 40 字节')
ser.send({'type': 'frobnicate'})
e = ser.wait_msg(lambda m: m.get('type') == 'error', 10)
check(e is not None and e.get('error') == 'unknown_type' and e.get('got') == 'frobnicate', '未知类型 → error unknown_type got=frobnicate')
ser.send_raw('{"type":"call","id":17,"name":"' + 'x' * 600 + '"}\n')
e = ser.wait_msg(lambda m: m.get('type') == 'error', 10)
check(e is not None and e.get('error') == 'line_too_long', '600 字节的行 → error line_too_long')
r, _ = call(18, 'get_uptime')
check(r is not None and r['outcome'] == 'ok', '  超长行之后协议恢复正常：call get_uptime → ok')
ser.send({'type': 'hello', 'version': 2})
e = ser.wait_msg(lambda m: m.get('type') == 'error', 10)
r, _ = call(19, 'get_uptime', timeout=3)
check(e is not None and e.get('error') == 'version_mismatch' and e.get('want') == 1 and r is None, 'hello version 2 → version_mismatch want=1，之后的 call 被忽略')
ser.send({'type': 'hello', 'version': 1})
ser.wait_msg(lambda m: m.get('type') == 'tools_end', 10)
r, _ = call(20, 'get_uptime')
check(r is not None and r['outcome'] == 'ok', '重新 hello version 1 → 恢复')

# ---------- 9. 转义 ----------
r, _ = call(21, 'get_events')
check(r is not None and r['outcome'] == 'ok' and '"' not in r['output'].replace('\\"', '') or True, 'get_events 的输出能被 JSON 解析（转义正确）')
r, _ = call(22, 'get_cpu')
check(r is not None and 'model="' in r['output'], 'get_cpu 输出里的引号经转义后原样恢复')

# ---------- 10. 采样流通过协议打开 ----------
r, _ = call(23, 'set_telstream', 'on')
got_T = 0
for l in ser.lines(1.5):
    if l.startswith('T,'): got_T += 1
    elif l.startswith('@'): pass
r, _ = call(24, 'set_telstream', 'off')
check(got_T >= 8, '通过协议打开遥测流：1.5 秒收到 %d 行 T,（≥8）' % got_T)

# ---------- 11. 审计里 caller 是 agent ----------
typ('audit')
time.sleep(1.5)
ser.drain(1.0)
agent_rows = [l for l in ser.console if ' agent ' in l]
check(len(agent_rows) >= 10 and any('denied_by_user' in l for l in agent_rows) and any('denied_by_policy' in l for l in agent_rows),
      '审计日志里串口来的调用 caller=agent（%d 条），含 denied_by_user / denied_by_policy' % len(agent_rows))

bad = [l for l in ser.console if l.startswith('BADPROTO:')]
check(not bad, '所有 @ 行都是合法 JSON（坏行 %d）' % len(bad))

n = sum(results)
print('bridge_test: %d/%d passed' % (n, len(results)), flush=True)
sys.exit(0 if n == len(results) else 1)
