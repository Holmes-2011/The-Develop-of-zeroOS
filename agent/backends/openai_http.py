"""OpenAI 兼容后端：任何暴露 /v1/chat/completions 的本地或远程模型都能接。

把 base_url 指向你自己的推理服务即可（vLLM / llama.cpp server / Ollama / 自研服务）。
模型和 Agent 循环、协议完全解耦。
"""
import json
import os
import urllib.request

from .base import Backend, Decision


class OpenAIBackend(Backend):
    name = "openai-http"

    def __init__(self, base_url=None, model=None, api_key=None, timeout=60):
        self.base_url = (base_url or os.environ.get("ZERO_AGENT_BASE_URL", "http://127.0.0.1:8000/v1")).rstrip("/")
        self.model = model or os.environ.get("ZERO_AGENT_MODEL", "local-model")
        self.api_key = api_key or os.environ.get("ZERO_AGENT_API_KEY", "not-needed")
        self.timeout = timeout

    def decide(self, messages, tools):
        body = {
            "model": self.model,
            "messages": messages,
            "tools": [{
                "type": "function",
                "function": {
                    "name": t["name"],
                    "description": t.get("description", ""),
                    "parameters": {"type": "object", "properties": {}, "additionalProperties": False},
                },
            } for t in tools],
            "tool_choice": "auto",
            "temperature": 0,
        }
        req = urllib.request.Request(
            self.base_url + "/chat/completions",
            data=json.dumps(body).encode("utf-8"),
            headers={"Content-Type": "application/json", "Authorization": "Bearer " + self.api_key},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=self.timeout) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        msg = data["choices"][0]["message"]
        calls = msg.get("tool_calls") or []
        if calls:
            fn = calls[0]["function"]
            args = fn.get("arguments", "")
            return Decision(tool=fn["name"], args=_args_to_kernel_string(args))
        return Decision(text=msg.get("content") or "")


def _args_to_kernel_string(raw):
    """内核的 args 是一个字符串（和 `tool <name> [args]` 一样）。

    约定：模型输出 {"args":"..."} 就直接用；否则把键值拼成 "k=v k=v"。
    """
    if isinstance(raw, str):
        try:
            obj = json.loads(raw)
        except json.JSONDecodeError:
            return raw
    else:
        obj = raw or {}
    if isinstance(obj, dict):
        if isinstance(obj.get("args"), str):
            return obj["args"]
        return " ".join("%s=%s" % (k, v) for k, v in obj.items() if v not in ("", None))
    return str(obj)
