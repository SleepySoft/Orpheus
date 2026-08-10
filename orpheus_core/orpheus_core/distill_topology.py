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
    # 具体块名优先于通用关键词（LevelDetect 含 pooliir、相干含窗/Saturation 等）
    (re.compile(r"leveldetect", re.I), "orpheus.builtin.level_detect", "builtin"),
    (re.compile(r"coherence|相干", re.I), "orpheus.builtin.coherence_matrix", "builtin"),
    (re.compile(r"sleepingbeauty", re.I), "orpheus.builtin.sleeping_beauty", "builtin"),
    (re.compile(r"pooliir", re.I), "orpheus.builtin.iir_bank", "builtin"),
    (re.compile(r"biquad", re.I), "orpheus.builtin.biquad", "builtin"),
    (re.compile(r"fir", re.I), "orpheus.builtin.fir", "builtin"),
    (re.compile(r"ifft", re.I), "orpheus.builtin.ifft", "builtin"),
    (re.compile(r"(?:r?fft|stft)", re.I), "orpheus.builtin.rfft", "builtin"),
    (re.compile(r"windowing|窗函数|窗", re.I), "orpheus.builtin.window", "builtin"),
    (re.compile(r"limiter", re.I), "orpheus.builtin.limiter", "builtin"),
    (re.compile(r"softclipper|clipper", re.I), "orpheus.builtin.soft_clipper", "builtin"),
    (re.compile(r"saturation", re.I), "orpheus.builtin.saturation", "builtin"),
    (re.compile(r"matrixmultiply|矩阵乘", re.I), "orpheus.builtin.matrix_mul", "builtin"),
    # 斜坡渐变混音矩阵（N表插值 + 一阶IIR斜坡）-> slc_matrix_mul
    (re.compile(r"slcmatrixmul", re.I), "orpheus.builtin.slc_matrix_mul", "builtin"),
    # FDP 频域系数施加（频域 bin × 系数矩阵 -> 多路频域输出）-> 矩阵乘
    (re.compile(r"applycoeff", re.I), "orpheus.builtin.matrix_mul", "substitute"),
    (re.compile(r"magnitude|平方|mag2|power", re.I), "orpheus.builtin.square", "builtin"),
    (re.compile(r"noiseslew", re.I), "orpheus.builtin.noise_slew", "builtin"),
    (re.compile(r"speedbounds", re.I), "orpheus.builtin.interp_lut", "substitute"),
    (re.compile(r"reverb", re.I), None, "missing"),
    (re.compile(r"switch", re.I), "orpheus.builtin.switch", "builtin"),
    (re.compile(r"spatialfader", re.I), "orpheus.builtin.fade", "substitute"),
    (re.compile(r"(?:output|input)_select|inputselect|router", re.I), "orpheus.builtin.output_router", "builtin"),
    (re.compile(r"selector", re.I), "orpheus.builtin.input_select", "substitute"),
    # 环绕离散选择器（多路并行 select）-> 变量选择器；须在 atmos 之前匹配
    (re.compile(r"selectsurround|selectleft|selectright|selectdiscrete", re.I), "orpheus.builtin.input_select", "substitute"),
    # 4 路并行输出分组（LeftAtmos/LeftFdp/...）-> 输出路由
    (re.compile(r"atmos", re.I), "orpheus.builtin.output_router", "substitute"),
    (re.compile(r"路由", re.I), "orpheus.builtin.output_router", "substitute"),
    (re.compile(r"send\d*out", re.I), "orpheus.builtin.output_router", "substitute"),
    # 输出路由端点（PreqOut/AudioOut）-> 输出路由
    (re.compile(r"preqout|audioout", re.I), "orpheus.builtin.output_router", "substitute"),
    (re.compile(r"inputmixer3d|downmix", re.I), "orpheus.builtin.input_mixer_3d", "builtin"),
    (re.compile(r"sumofelements|sum|求和", re.I), "orpheus.builtin.mixer", "substitute"),
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
    (re.compile(r"psd", re.I), "orpheus.builtin.psd", "builtin"),
    (re.compile(r"bufferin|bufferout|块缓冲", re.I), None, "na"),
    (re.compile(r"正弦调制", re.I), "orpheus.builtin.sine_mod", "builtin"),
]


def map_block(name: str, raw: str = "") -> str:
    """块名（附原始片段）→ 组件 id；未映射返回占位组件 id。

    用 name + raw 匹配，避免 "Sum"（流程文本 "Sum(...)"）这类只有原始片段
    才带关键字的块映射不到真实组件。
    """
    text = f"{name} {raw}"
    for rx, comp, _status in _RULES:
        if rx.search(text):
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


_NOISE_RE = re.compile(
    r"map$|路径|缓冲|未用|置零|rampcoeff|powf|=|bufferin|bufferout|"
    r"^buffer|bufferref|buffermic|delaybuffer|ratetransition", re.I
)


def _is_noise(block: dict[str, str]) -> bool:
    """数据表 / 描述性片段不是可实现的块：路由映射表、延迟缓冲、置零说明等。"""
    return bool(_NOISE_RE.search(block["name"]))


def _chain_blocks(chain: dict[str, Any], sub_id: str) -> list[dict[str, str]]:
    """链 flow 文本 → 块列表（过滤噪声；空则保留链名占位）。"""
    blocks = parse_flow(chain.get("flow"))
    blocks = [b for b in blocks if not _is_noise(b)]
    if not blocks:
        blocks = [{"name": chain.get("label") or chain.get("id") or sub_id, "params": "", "raw": ""}]
    return blocks


def _blocks_to_sub(sub_id: str, name: str, blocks: list[dict[str, str]]) -> dict[str, Any]:
    """块列表 → 子组件（串接 b0..bN，端口 in/out）。"""
    inner_nodes: list[dict[str, Any]] = []
    for j, b in enumerate(blocks):
        inner_nodes.append(
            {
                "id": f"b{j}",
                "component": map_block(b["name"], b["raw"]),
                "label": b["name"],
                "params": {"note": b["params"]},
                "position": {"x": j * 220, "y": 60},
            }
        )
    inner_edges = [
        {"from": f"b{j}:out", "to": f"b{j + 1}:in"} for j in range(len(inner_nodes) - 1)
    ]
    return {
        "id": sub_id,
        "name": name,
        "description": f"蒸馏链：{name}",
        "ports": [
            {"id": "in", "direction": "input", "maps_to": "b0:in"},
            {"id": "out", "direction": "output", "maps_to": f"b{len(inner_nodes) - 1}:out"},
        ],
        "graph": {"nodes": inner_nodes, "connections": inner_edges},
    }


def build_topology(
    model_tree: dict[str, Any],
    *,
    sample_rate: int = 48000,
    channels: int = 22,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """model_tree → (主图 graph, 子模块列表)。

    task_flows 带结构化 chains/blocks 时：
    - call_interval=1 的 task（TID0）= 主音频链（基础速率，串接 sys_in..sys_out）；
    - call_interval>1 的 task = 降速率分析抽头：主图插入 downrate(factor=call_interval)
      从 sys_in 抽头 → 抽头子模块 → 分析抽头终点（embed_out），体现多速率域。

    - mode=inline 的 task（如 FDP）= 内联多速率：chains 并入主链，不生成死路抽头。
    旧格式（task_flows 无 chains/blocks）回退为全部链串接一条主链。
    """
    chains_by_id = {c.get("id"): c for c in model_tree.get("chains") or [] if c.get("id")}
    task_flows = model_tree.get("task_flows") or []
    structured = [t for t in task_flows if t.get("chains") or t.get("blocks")]
    if not structured:
        task_flows = [{"tid": 0, "call_interval": 1, "chains": list(chains_by_id)}]

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

    main_chain_ids: list[str] = []
    taps: list[tuple[int, int, str, list[dict[str, str]]]] = []
    for t in task_flows:
        call_interval = int(t.get("call_interval") or 1)
        if call_interval <= 1 or t.get("mode") == "inline":
            for cid in t.get("chains") or []:
                if cid in chains_by_id:
                    main_chain_ids.append(cid)
        else:
            tid = int(t.get("tid") or 0)
            label = t.get("label") or f"TID{tid}"
            blocks: list[dict[str, str]] = []
            for cid in t.get("chains") or []:
                ch = chains_by_id.get(cid)
                if ch:
                    blocks += parse_flow(ch.get("flow"))
            for b in t.get("blocks") or []:
                blocks.append({"name": str(b), "params": "", "raw": str(b)})
            blocks = [b for b in blocks if not _is_noise(b)]
            if blocks:
                taps.append((tid, call_interval, label, blocks))

    prev = "sys_in"
    for i, cid in enumerate(main_chain_ids):
        ch = chains_by_id[cid]
        sub_id = _sanitize_id(cid, f"chain_{i}")
        subs.append(_blocks_to_sub(sub_id, ch.get("label") or cid, _chain_blocks(ch, sub_id)))
        main_nodes.append(
            {
                "id": sub_id,
                "component": f"sub:{sub_id}",
                "position": {"x": 40 + (i + 1) * 280, "y": 200},
            }
        )
        main_edges.append({"from": f"{prev}:out", "to": f"{sub_id}:in"})
        prev = sub_id

    # 降速率分析抽头：downrate(factor=call_interval) -> tap 子模块 -> 分析抽头终点
    for k, (tid, call_interval, label, blocks) in enumerate(taps):
        tap_id = f"tap_{tid}"
        dr_id = f"downrate_{tid}"
        sink_id = f"sink_{tid}"
        subs.append(_blocks_to_sub(tap_id, label, blocks))
        x = 40 + (k + 1) * 300
        main_nodes.append(
            {
                "id": dr_id,
                "component": "orpheus.builtin.downrate",
                "label": f"{label} ÷{call_interval}",
                "params": {"factor": call_interval, "channels": channels},
                "position": {"x": x, "y": 560},
            }
        )
        main_nodes.append(
            {
                "id": tap_id,
                "component": f"sub:{tap_id}",
                "position": {"x": x + 180, "y": 560},
            }
        )
        main_nodes.append(
            {
                "id": sink_id,
                "component": "orpheus.builtin.embed_out",
                "label": f"分析抽头终点 TID{tid}",
                "params": {"channels": channels, "sample_rate": sample_rate},
                "position": {"x": x + 360, "y": 560},
            }
        )
        main_edges.append({"from": "sys_in:out", "to": f"{dr_id}:in"})
        main_edges.append({"from": f"{dr_id}:out", "to": f"{tap_id}:in"})
        main_edges.append({"from": f"{tap_id}:out", "to": f"{sink_id}:in"})

    main_nodes.append(
        {
            "id": "sys_out",
            "component": "orpheus.builtin.embed_out",
            "label": "系统输出",
            "params": {"channels": channels, "sample_rate": sample_rate},
            "position": {"x": 40 + (len(main_chain_ids) + 1) * 280, "y": 200},
        }
    )
    main_edges.append({"from": f"{prev}:out", "to": "sys_out:in"})
    return {"nodes": main_nodes, "connections": main_edges}, subs
