"""串口/TCP 链路：一行一条消息，@ 开头是协议 JSON，其余是控制台文字。"""
import json
import select
import socket


class Link:
    def __init__(self, host="127.0.0.1", port=5601, timeout=10.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setblocking(False)
        self.buf = b""
        self.console = []

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def send(self, obj):
        self.sock.sendall((json.dumps(obj, ensure_ascii=True) + "\n").encode("ascii"))

    def _readline(self, deadline):
        while b"\n" not in self.buf:
            remain = deadline - __import__("time").monotonic()
            if remain <= 0:
                return None
            r, _, _ = select.select([self.sock], [], [], remain)
            if not r:
                return None
            chunk = self.sock.recv(4096)
            if not chunk:
                return None
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8", "replace").rstrip("\r")

    def read(self, timeout=5.0):
        """返回 ('msg', dict) / ('console', str) / (None, None)。"""
        import time
        deadline = time.monotonic() + timeout
        line = self._readline(deadline)
        if line is None:
            return None, None
        if line.startswith("@"):
            try:
                return "msg", json.loads(line[1:])
            except json.JSONDecodeError:
                return "console", line
        self.console.append(line)
        return "console", line
