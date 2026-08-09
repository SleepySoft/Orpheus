"""蒸馏模型拓扑展开。

把 `model_tree.chains` 里的流程文本（"BlockA(param) -> BlockB -> ..."）解析成
可浏览的拓扑图：每条链展开为一个子模块，块展开为节点；能映射到内置组件的块
用真实组件 id，其余用占位组件 id（UI 显示为「组件缺失」，不影响看拓扑）。
"""

from __future__ import annotations

import re
from typing import Any

# 未映射块的占位组件 id（当前不是真实组件，UI 会标红「组件缺失」）
PLACEHOLDER_COMPONENT = "orpheus.builtin.placeholder"

# 块名 → (组件 id | None, 状态)，顺序即优先级；与前端 ProjectTree 的徽章规则一致
_RULES: list[tuple[re.Pattern[str], str | None, str]] = [
    (re.compile(r"pooliir", re.I), "orpheus.builtin.biquad_bank", "substitute"),
    (re.compile(r"biquad", re.I), "orpheus.builtin.biquad", "builtin"),
    (re.compile(r"fir", re.I), "orpheus.builtin.fir", "builtin"),
    (re.compile(r"(?:r?fft|ifft|stft)", re.I), None, "missing"),
    (re.compile(r"windowing|窗函数|窗", re.I), "orpheus.builtin.window", "builtin"),
    (re.compile(r"limiter", re.I), "orpheus.builtin.limiter", "builtin"),
    (re.compile(r"softclipper|clipper", re.I), "orpheus.builtin.soft_clipper", "builtin"),
    (re.compile(r"saturation", re.I), "orpheus.builtin.saturation", "builtin"),
    (re.compile(r"matrixmultiply|矩阵乘", re.I), "orpheus.builtin.matrix_mul", "builtin"),
    (re.compile(r"coherence|相干", re.I), None, "missing"),
    (re.compile(r"noiseslew", re.I), "orpheus.builtin.noise_slew", "builtin"),
    (re.compile(r"speedbounds", re.I), None, "missing"),
    (re.compile(r"leveldetect", re.I), "orpheus.builtin.level_detect", "builtin"),
    (re.compile(r"sleepingbeauty", re.I), None, "missing"),
    (re.compile(r"reverb", re.I), None, "missing"),
    (re.compile(r"switch", re.I), "orpheus.builtin.switch", "builtin"),
    (re.compile(r"spatialfader", re.I), "orpheus.builtin.fade", "substitute"),
    (re.compile(r"selector", re.I), "orpheus.builtin.input_select", "substitute"),
    (re.compile(r"(?:output|input)_select|inputselect|\broster\b", re.I), "orpheus.builtin.output_router", "builtin"),
    (re.compile(r"路由", re.I), "orpheus.builtin.output_router", "substitute"),
    (re.compile(r"downmix", re.I), "orpheus.builtin.mixer", "substitute"),
    (re.compile(r"sumofelements|sum\(|求和", re.I), "orpheus.builtin.mixer", "substitute"),
    (re.compile(r"lpf|lowpass|low.?pass", re.I), "orpheus.builtin.biquad", "builtin"),
    (re.compile(r"volume", re.I), "orpheus.builtin.gain", "substitute"),
    (re.compile(r"balance", re.I), "orpheus.builtin.balance", "builtin"),
    (re.compile(r"delay", re.I), "orpheus.builtin.delay", "builtin"),
    (re.compile(r"gain", re.I), "orpheus.builtin.gain", "builtin"),
    (re.compile(r"mute", re.I), "orpheus.builtin.mute", "builtin"),
    (re.compile(r"fade", re.I), "orpheus.builtin.fade", "builtin"),
    (re.compile(r"bass", re.I), "orpheus.builtin.bass", "builtin"),
    (re.compile(r"midrange", re.I), "orpheus.builtin.midrange", "builtin"),
    (re.compile(r"treble", re.I), "orpheus.builtin.treble", "builtin"),
    (re.compile(r"mixer", re.I), "orpheus.builtin.mixer", "builtin"),
    (re.compile(r"psd|smooth", re.I), None, "missing"),
    (re.compile(r"bufferin|bufferout|块缓冲", re.I), None, "na"),
    (re.compile(r"正弦调制", re.I), None, "missing"),
]


def map_block(name: str) -> str:
    """块名 → 组件 id；未映射返回占位组件 id。"""
    for rx, comp, _status in _RULES:
        if rx.search(name):
            return comp or PLACEHOLDER_COMPONENT
    return PLACEHOLDER_COMPONENT


def _split_plus(text: str) -> list[str]:
    """按顶层 ' + ' 切分（括号内不切），用于同段并列的多个块。"""
    parts: list[str] = []
    cur: list[str] = []
    depth = 0
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        if depth == 0 and text[i : i + 3] == " + ":
            parts.append("".join(cur))
            cur = []
            i += 3
            continue
        cur.append(ch)
        i += 1
    parts.append("".join(cur))
    return parts


def _split_flow(flow: str) -> list[str]:
    """按顶层 `->` / `→` 切块（括号内不切，避免 "30ch->32ch" 被误拆）。"""
    parts: list[str] = []
    cur: list[str] = []
    depth = 0
    i = 0
    n = len(flow)
    while i < n:
        ch = flow[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        if depth == 0:
            if flow.startswith("->", i):
                parts.append("".join(cur))
                cur = []
                i += 2
                continue
            if ch == "→":
                parts.append("".join(cur))
                cur = []
                i += 1
                continue
        cur.append(ch)
        i += 1
    parts.append("".join(cur))
    return parts


def parse_flow(flow: str | None) -> list[dict[str, str]]:
    """流程文本 → 块列表 [{name, params, raw}]。

    - 按 `->` / `→` 切块；
    - 段内按顶层 ` + ` 再切（如 "RFFT(10ch) + 窗 + Coherence(...)"）；
    - 去掉 "TIDn:" 前缀；
    - name 取到第一个 `(` / `[` 为止，括号内容作为 params。
    """
    blocks: list[dict[str, str]] = []
    for seg in _split_flow(flow or ""):
        for piece in _split_plus(seg):
            piece = piece.strip()
            if not piece:
                continue
            piece = re.sub(r"^TID\d+\s*:\s*", "", piece).strip()
            if not piece:
                continue
            m = re.match(r"^(.*?)(?=\s*[\(\[]|$)", piece)
            name = (m.group(1) if m else piece).strip() or piece[:12]
            pm = re.search(r"\((.*?)\)", piece)
            blocks.append(
                {
                    "name": name,
                    "params": pm.group(1) if pm else "",
                    "raw": piece,
                }
            )
    return blocks


def _sanitize_id(text: str, fallback: str) -> str:
    out = re.sub(r"[^A-Za-z0-9_-]", "_", text or "").strip("_")
    return out or fallback


def build_topology(
    model_tree: dict[str, Any],
    *,
    sample_rate: int = 48000,
    channels: int = 22,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """model_tree.chains → (主图 graph, 子模块列表)。每条链串接为一条主链。"""
    chains = model_tree.get("chains") or []
    subs: list[dict[str, Any]] = []
    main_nodes: list[dict[str, Any]] = [
        {
            "id": "sys_in",
            "component": "orpheus.builtin.embed_in",
            "label": "系统输入",
            "params": {"channels": channels, "sample_rate": sample_rate},
            "position": {"x": 40, "y": 200},
        }
    ]
    main_edges: list[dict[str, str]] = []
    prev = "sys_in"
    for i, ch in enumerate(chains):
        sub_id = _sanitize_id(ch.get("id") or "", f"chain_{i}")
        blocks = parse_flow(ch.get("flow"))
        if not blocks:
            blocks = [{"name": ch.get("label") or ch.get("id") or sub_id, "params": "", "raw": ""}]
        inner_nodes: list[dict[str, Any]] = []
        for j, b in enumerate(blocks):
            inner_nodes.append(
                {
                    "id": f"b{j}",
                    "component": map_block(b["name"]),
                    "label": b["name"],
                    "params": {"note": b["params"]},
                    "position": {"x": j * 220, "y": 60},
                }
            )
        inner_edges = [
            {"from": f"b{j}:out", "to": f"b{j + 1}:in"} for j in range(len(inner_nodes) - 1)
        ]
        subs.append(
            {
                "id": sub_id,
                "name": ch.get("label") or ch.get("id") or sub_id,
                "description": f"蒸馏链：{ch.get('label') or ch.get('id') or sub_id}",
                "ports": [
                    {"id": "in", "direction": "input", "maps_to": "b0:in"},
                    {"id": "out", "direction": "output", "maps_to": f"b{len(inner_nodes) - 1}:out"},
                ],
                "graph": {"nodes": inner_nodes, "connections": inner_edges},
            }
        )
        main_nodes.append(
            {
                "id": sub_id,
                "component": f"sub:{sub_id}",
                "position": {"x": 40 + (i + 1) * 280, "y": 200},
            }
        )
        main_edges.append({"from": f"{prev}:out", "to": f"{sub_id}:in"})
        prev = sub_id
    main_nodes.append(
        {
            "id": "sys_out",
            "component": "orpheus.builtin.embed_out",
            "label": "系统输出",
            "params": {"channels": channels, "sample_rate": sample_rate},
            "position": {"x": 40 + (len(chains) + 1) * 280, "y": 200},
        }
    )
    main_edges.append({"from": f"{prev}:out", "to": "sys_out:in"})
    return {"nodes": main_nodes, "connections": main_edges}, subs
