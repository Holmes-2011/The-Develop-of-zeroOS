"""zeroOS 桥接协议 v1 的宿主侧实现（见 docs/agent-bridge-protocol.md）。"""
from .link import Link


class BridgeError(RuntimeError):
    pass


class Bridge:
    def __init__(self, host="127.0.0.1", port=5601, client="zero-agent/0.1"):
        self.link = Link(host, port)
        self.client = client
        self.tools = []
        self.welcome = None
        self.next_id = 1
        self.console = []

    def handshake(self, timeout=20.0):
        """连接可能早于内核启动，所以重复 hello，直到收到 tools_end。"""
        import time
        deadline = time.monotonic() + timeout
        self.tools = []
        self.link.send({"type": "hello", "version": 1, "client": self.client})
        last_hello = time.monotonic()
        while time.monotonic() < deadline:
            kind, msg = self.link.read(0.5)
            if kind is None:
                if time.monotonic() - last_hello > 1.0:
                    self.link.send({"type": "hello", "version": 1, "client": self.client})
                    last_hello = time.monotonic()
                    self.tools = []
                continue
            if kind == "console":
                self.console.append(msg)
                continue
            t = msg.get("type")
            if t == "welcome":
                self.welcome = msg
                self.tools = []
            elif t == "tool":
                self.tools.append(msg)
            elif t == "tools_end":
                return self.welcome, self.tools
            elif t == "error" and msg.get("error") == "version_mismatch":
                raise BridgeError("version mismatch")
        raise BridgeError("handshake: no reply in %.0fs" % timeout)

    def call(self, name, args="", timeout=5.0):
        cid = self.next_id
        self.next_id += 1
        self.link.send({"type": "call", "id": cid, "name": name, "args": args})
        while True:
            kind, msg = self.link.read(timeout)
            if kind is None:
                raise BridgeError("call %s: timeout" % name)
            if kind == "console":
                self.console.append(msg)
                continue
            t = msg.get("type")
            if t == "confirm":
                # 需要用户在内核的确认框里按 y/n；这里继续等最终结果
                continue
            if t == "result" and msg.get("id") == cid:
                return msg
            if t == "error":
                raise BridgeError("call %s: %s" % (name, msg.get("error", "?")))

    def close(self):
        self.link.close()
