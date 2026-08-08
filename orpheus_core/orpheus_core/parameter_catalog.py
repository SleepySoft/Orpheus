"""数据点编目：把工程（含嵌套子组件）展开为可读的树形数据 layout。

与前端 ParamBrowser 的树逻辑同构（flatId = 实例路径按 ``__`` 连接，
kind 分类 = manifest `kind` 或 readback 推断），服务端用于：
- 测试/验证数据 layout（层级、分类、flatId）；
- 生成可读的导出 JSON（调音值 + Bulk 数组 + 探针占位）；
- 导入回写（按 flatId 定位叶子节点）。
"""

from __future__ import annotations

import copy
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from orpheus_core.project import Graph, Project, Subcomponent
from orpheus_core.registry import ComponentInfo, Registry

KIND_ORDER = ["setting", "bulk", "probe", "command", "state"]

_FLOAT_RE = re.compile(r"[\s,;]+")
SUB_PREFIX = "sub:"

"""32 位数据 ID：用途（purpose）按使用频率排序，RTC 第一；形式（form）是独立维度。"""
ID_KIND_BITS = {"RTC": 0x0, "TUNE": 0x1, "PROBE": 0x2, "STATE": 0x3, "CUSTOM": 0x4}
ID_SLOT_MODULE = 0xFFFF  # 模块包条目占用的槽号（不与数据点槽冲突）


def is_subcomponent_ref(component: str) -> bool:
    return component.startswith(SUB_PREFIX)


def subcomponent_id(component: str) -> str:
    """'sub:accumulator' -> 'accumulator'。"""
    return component[len(SUB_PREFIX):]


def kind_of(param: dict[str, Any]) -> str:
    """数据点类别：显式 kind 优先；否则按 readback 语义推断（与前端一致）。"""
    kind = param.get("kind")
    if kind:
        return kind
    if param.get("readback") and not param.get("persistent") and not param.get("affects_signature"):
        return "probe"
    return "setting"


def id_kind_of(param: dict[str, Any]) -> str:
    """数据点用途（purpose，32 位 ID kind）：
    - 命令 → RTC；探针 → PROBE；state → STATE；bulk 参数 → TUNE（形式为 bulk）；
    - 实时可调参数（immediate/block_boundary/smoothed/transactional）→ RTC；
    - 其余（restart_required / 影响签名 / 系数等配置）→ TUNE。"""
    k = param.get("kind")
    if k == "probe":
        return "PROBE"
    if k == "state":
        return "STATE"
    if k == "command":
        return "RTC"
    if k == "bulk":
        return "TUNE"
    if k == "custom":
        return "CUSTOM"
    if param.get("readback") and not param.get("persistent") and not param.get("affects_signature"):
        return "PROBE"
    if param.get("update_policy") in ("immediate", "block_boundary", "smoothed", "transactional"):
        return "RTC"
    return "TUNE"


def id_form_of(param: dict[str, Any]) -> str:
    """数据点形式（form，与用途正交）：bulk 参数/运行期槽 → BULK，否则 SCALAR。"""
    if param.get("runtime") or param.get("kind") == "bulk":
        return "BULK"
    return "SCALAR"


def id_value(kind: str, module_id: int, slot: int) -> int:
    return (ID_KIND_BITS[kind] << 28) | ((module_id & 0xFF) << 16) | (slot & 0xFFFF)


def parse_float_list(text: Any) -> list[float]:
    """从逗号/空白/分号分隔文本解析浮点数组（Bulk 参数）。"""
    if text is None:
        return []
    out: list[float] = []
    for token in _FLOAT_RE.split(str(text)):
        if not token:
            continue
        try:
            out.append(float(token))
        except ValueError:
            continue
    return out


@dataclass
class CatalogEntry:
    """一个叶子节点的数据点集合（含层级路径与 flatId）。"""

    flat_id: str
    view_path: list[str]  # 前端视图定位：['main'] 或 ['sub:<id>', ...]
    node_id: str
    component: str
    component_name: str
    path: list[dict[str, str]] = field(default_factory=list)  # [{id, label}]
    by_kind: dict[str, list[dict[str, Any]]] = field(default_factory=dict)
    node_params: dict[str, Any] = field(default_factory=dict)

    def params_of(self, kind: str) -> list[dict[str, Any]]:
        return self.by_kind.get(kind, [])


def build_catalog(project: Project, registry: Registry) -> list[CatalogEntry]:
    """递归展开主图与子组件，产出叶子条目（与 flatten 的 flatId 规则一致）。"""
    subs = {s.id: s for s in project.subcomponents}
    entries: list[CatalogEntry] = []

    def walk(graph: Graph, view_path: list[str], path: list[dict[str, str]]) -> None:
        for node in graph.nodes.values():
            if is_subcomponent_ref(node.component):
                if any(p["id"] == node.id for p in path):
                    continue  # 环保护（后端 flatten 会报错）
                sub = subs.get(subcomponent_id(node.component))
                if sub is None:
                    continue
                walk(
                    sub.graph,
                    [*view_path, node.component],
                    [*path, {"id": node.id, "label": node.id}],
                )
                continue
            info = registry.get(node.component)
            by_kind: dict[str, list[dict[str, Any]]] = {k: [] for k in KIND_ORDER}
            for p in (info.manifest.get("parameters", []) if info else []):
                by_kind.setdefault(kind_of(p), []).append(p)
            for bs in (info.manifest.get("bulk_slots", []) if info else []):
                by_kind["bulk"].append({**bs, "runtime": True})
            node_path = [*path, {"id": node.id, "label": node.id}]
            entries.append(
                CatalogEntry(
                    flat_id="__".join(x["id"] for x in node_path),
                    view_path=view_path,
                    node_id=node.id,
                    component=node.component,
                    component_name=(info.manifest.get("name", node.component) if info else node.component),
                    path=node_path,
                    by_kind=by_kind,
                    node_params=dict(node.params),
                )
            )

    walk(project.graph, ["main"], [])
    return entries


def _locate_node(project: Project, entry: CatalogEntry):
    """按 path 实例链（如 front → eq_bank → bq）逐层定位叶子节点对象。"""
    subs = {s.id: s for s in project.subcomponents}
    container: Graph = project.graph
    for step in entry.path[:-1]:
        instance = container.nodes.get(step["id"])
        if instance is None or not is_subcomponent_ref(instance.component):
            return None
        sub = subs.get(subcomponent_id(instance.component))
        if sub is None:
            return None
        container = sub.graph
    return container.nodes.get(entry.node_id)


def export_payload(project: Project, registry: Registry) -> dict[str, Any]:
    """可读导出：nodes[{node, path, component, component_name, values, bulk, probes}]。"""
    nodes: list[dict[str, Any]] = []
    for e in build_catalog(project, registry):
        values: dict[str, Any] = {}
        bulk: dict[str, Any] = {}
        for p in e.params_of("setting"):
            values[p["id"]] = e.node_params.get(p["id"], p.get("default"))
        for p in e.params_of("bulk"):
            if not p.get("runtime"):
                bulk[p["id"]] = parse_float_list(e.node_params.get(p["id"]))
        nodes.append(
            {
                "node": e.flat_id,
                "path": [x["label"] for x in e.path],
                "component": e.component,
                "component_name": e.component_name,
                "values": values,
                "bulk": bulk,
                "probes": {},
            }
        )
    return {
        "format": "orpheus.parameters",
        "version": 1,
        "project": project.metadata.get("name", ""),
        "exported_at": "",
        "nodes": nodes,
    }


def apply_payload(project: Project, payload: dict[str, Any], registry: Registry) -> int:
    """按 flatId 把导出/预设值回写工程节点参数；返回应用节点数。"""
    by_flat = {e.flat_id: e for e in build_catalog(project, registry)}
    applied = 0
    for item in payload.get("nodes", []):
        entry = by_flat.get(item.get("node"))
        if entry is None:
            continue
        node = _locate_node(project, entry)
        if node is None:
            continue
        for key, value in (item.get("values") or {}).items():
            node.params[key] = value
        for key, arr in (item.get("bulk") or {}).items():
            node.params[key] = ", ".join(str(v) for v in arr)
        applied += 1
    return applied


def render_tree(project: Project, registry: Registry) -> str:
    """把数据 layout 渲染为可读文本树（测试/脚本输出用）。"""
    entries = build_catalog(project, registry)
    lines = [f"# 数据点 layout（{project.metadata.get('name', project)}）"]
    for kind in KIND_ORDER:
        items = [e for e in entries if e.params_of(kind)]
        if not items:
            continue
        lines.append(f"## {kind} ({sum(len(e.params_of(kind)) for e in items)})")
        for e in items:
            head = " · ".join(x["label"] for x in e.path)
            lines.append(f"  [{e.flat_id}] {e.component_name} ({head})")
            for p in e.params_of(kind):
                runtime = " [运行期槽]" if p.get("runtime") else ""
                lines.append(
                    f"    - {p.get('name', p['id'])} ({p['id']})"
                    f"{runtime} {p.get('unit', '')}"
                )
    return "\n".join(lines)
