class Decision:
    def __init__(self, tool=None, args="", text=None):
        self.tool = tool
        self.args = args
        self.text = text


class Backend:
    name = "base"

    def decide(self, messages, tools):
        """返回 Decision：要么调用工具，要么给最终回答。"""
        raise NotImplementedError
