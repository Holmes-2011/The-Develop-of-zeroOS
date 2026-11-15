from .base import Backend, Decision


class ScriptedBackend(Backend):
    """确定性后端：按预置计划依次调用工具，最后给一句回答。用于测试和演示。"""

    name = "scripted"

    def __init__(self, plan):
        self.plan = list(plan)
        self.i = 0

    def decide(self, messages, tools):
        if self.i >= len(self.plan):
            return Decision(text="scripted plan finished")
        step = self.plan[self.i]
        self.i += 1
        if "final" in step:
            return Decision(text=step["final"])
        return Decision(tool=step["tool"], args=step.get("args", ""))
