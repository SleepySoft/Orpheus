"""可运行占位模块的接口与显式路由契约。"""

from __future__ import annotations

import json
import re
from typing import Any

COMPONENT_ID = "orpheus.builtin.placeholder"
DEFAULT_INTERFACE = '[{"id":"in","direction":"input","channels":2},{"id":"out","direction":"output","channels":2}]'
MAX_PORTS = 32


def placeholder_ports(params: dict[str, Any]) -> list[dict[str, Any]]:
    try:
        ports = json.loads(params.get("interface", DEFAULT_INTERFACE))
    except (TypeError, ValueError) as exc:
        raise ValueError("占位模块 interface 必须是 JSON 端口数组") from exc
    if not isinstance(ports, list) or not ports or len(ports) > MAX_PORTS * 2:
        raise ValueError("占位模块须声明 1 至 64 个端口")
    seen: set[str] = set()
    result: list[dict[str, Any]] = []
    counts = {"input": 0, "output": 0}
    for port in ports:
        if not isinstance(port, dict) or set(port) - {"id", "direction", "channels"}:
            raise ValueError("占位端口只支持 id、direction、channels（同 Task 的 f32 音频）")
        port_id = port.get("id")
        direction = port.get("direction")
        channels = port.get("channels")
        if not isinstance(port_id, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", port_id):
            raise ValueError("占位端口 id 必须是字母/下划线开头的标识符")
        if port_id in seen:
            raise ValueError(f"占位端口 id 重复：{port_id}")
        if direction not in counts:
            raise ValueError(f"占位端口 {port_id} 的 direction 必须是 input/output")
        if type(channels) is not int or not 1 <= channels <= 64:
            raise ValueError(f"占位端口 {port_id} 的 channels 必须为 1 至 64 的整数")
        seen.add(port_id)
        counts[direction] += 1
        if counts[direction] > MAX_PORTS:
            raise ValueError("占位模块每个方向最多 32 个端口")
        result.append({**port, "type": "audio", "sample_format": "f32"})
    return result


def validate_routes(params: dict[str, Any], ports: list[dict[str, Any]]) -> None:
    mode = params.get("mode", "passthrough")
    if mode not in {"passthrough", "silence"}:
        raise ValueError("占位模块 mode 必须为 passthrough/silence")
    if mode == "silence":
        return
    inputs = [p for p in ports if p["direction"] == "input"]
    outputs = [p for p in ports if p["direction"] == "output"]
    routes = params.get("routes", "0")
    if not isinstance(routes, str):
        raise ValueError("占位模块 routes 必须是逗号分隔的输入序号")
    tokens = routes.split(",") if routes.strip() else []
    if len(tokens) != len(outputs):
        raise ValueError("占位模块 routes 必须为每个输出声明一个输入序号；-1 表示置零")
    for output, token in zip(outputs, tokens):
        if not re.fullmatch(r"\s*-?\d+\s*", token):
            raise ValueError(f"占位模块 routes 序号无效：{token!r}")
        index = int(token)
        if index < -1 or index >= len(inputs):
            raise ValueError(f"占位模块 routes 输入序号越界：{index}")
        if index >= 0 and inputs[index]["channels"] != output["channels"]:
            raise ValueError(f"占位模块透传通道不匹配：{inputs[index]['id']} → {output['id']}")
