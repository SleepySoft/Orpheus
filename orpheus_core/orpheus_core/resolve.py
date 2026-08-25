"""目标平台与 alter 组解析。

alter 由用户在画布上声明（节点级 ``alters``，同图内），引擎只做两件事：

1. **合规校验**：组内成员的组件接口必须一致（端口集合一致、共享参数类型一致），
   否则编译报错；
2. **平台解析**：整链按「每节点可达平台并集 → 跨节点交集」判定可用平台；
   选定平台后每组只激活一个成员（锚定成员优先），未激活成员不参与编译，
   锚定成员的连线重映射到激活成员（宏语义：只选一条路径）。

平台判定：组件未声明 platforms = 无指定平台（可移植，任何平台可用）；
声明了标签则只在这些平台可用；标签有层次（platforms.yaml，父平台隐含子平台）。
"""

from __future__ import annotations

import copy
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

import yaml

from orpheus_core.compiler import CompileError
from orpheus_core.project import Graph, Node, Project, Subcomponent
from orpheus_core.registry import ComponentInfo, Registry

_PLATFORMS_FILE = Path(__file__).parent / "platforms.yaml"


class ResolutionError(CompileError):
    """alter 声明非法或平台不可达。"""


@dataclass
class Resolution:
    platform: str  # 选定的具体平台（win / dsp / ...）
    groups: dict[str, list[str]] = field(default_factory=dict)  # 锚定节点 -> 组内成员
    active: dict[str, str] = field(default_factory=dict)  # 锚定节点 -> 激活成员 id
    warnings: list[str] = field(default_factory=list)


def load_platforms(path: Path | None = None) -> dict[str, list[str]]:
    """平台表：父平台 -> 子平台列表（数据驱动，一处声明）。"""
    p = path or _PLATFORMS_FILE
    with open(p, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    return dict(data.get("platforms", {}))


def all_platforms(platforms: dict[str, list[str]]) -> set[str]:
    """平台表全部平台（含子平台）。"""
    out = set(platforms)
    for children in platforms.values():
        out.update(children)
    return out


def _descendants(tag: str, platforms: dict[str, list[str]]) -> set[str]:
    """tag 及其所有子平台（族标签覆盖子平台：标 dsp 支持未来的 dsp 子平台）。"""
    out = {tag}
    changed = True
    while changed:
        changed = False
        for parent, children in platforms.items():
            if parent in out:
                for child in children:
                    if child not in out:
                        out.add(child)
                        changed = True
    return out


def component_supports(
    comp: ComponentInfo,
    platform: str,
    platforms: dict[str, list[str]],
) -> bool:
    """组件是否支持某平台：未声明 platforms（无指定平台）= 支持一切；
    声明了标签则平台 ∈ 标签或其子平台。"""
    if not comp.platforms:
        return True
    return any(platform in _descendants(tag, platforms) for tag in comp.platforms)


def _port_signature(comp: ComponentInfo) -> set[tuple[str, str]]:
    return {(p["id"], p["direction"]) for p in comp.manifest.get("ports", [])}


def _param_types(comp: ComponentInfo) -> dict[str, str]:
    return {p["id"]: str(p.get("type", "")) for p in comp.manifest.get("parameters", [])}


def check_group_compliance(
    members: list[Node],
    registry: Registry,
) -> None:
    """alter 组成员合规：端口集合一致、共享参数类型一致，否则报错。"""
    comps: list[ComponentInfo] = []
    for node in members:
        comp = registry.get(node.component)
        if comp is None:
            raise ResolutionError(
                f"alter 组节点 {node.id} 组件不存在: {node.component}"
            )
        comps.append(comp)
    base_ports = _port_signature(comps[0])
    base_params = _param_types(comps[0])
    for comp in comps[1:]:
        ports = _port_signature(comp)
        if ports != base_ports:
            raise ResolutionError(
                f"alter 组接口不一致（端口不同）："
                f"{comps[0].id} {sorted(base_ports)} vs {comp.id} {sorted(ports)}"
            )
        params = _param_types(comp)
        for pid, ptype in params.items():
            if pid in base_params and base_params[pid] != ptype:
                raise ResolutionError(
                    f"alter 组参数类型不一致：{comps[0].id}.{pid}"
                    f"({base_params[pid]}) vs {comp.id}.{pid}({ptype})"
                )


def _build_groups(nodes: Iterable[Node], graph: Graph) -> list[list[str]]:
    """按节点 alters 建无向组（同一图内），校验引用存在。"""
    by_id = {n.id: n for n in nodes}
    parent: dict[str, str] = {}

    def find(x: str) -> str:
        while parent.get(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a: str, b: str) -> None:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    for node in nodes:
        parent[node.id] = node.id
    for node in nodes:
        for alt in node.alters:
            if alt not in by_id:
                raise ResolutionError(
                    f"节点 {node.id} 的 alter 引用不存在的同图节点: {alt}"
                )
            union(node.id, alt)

    groups_map: dict[str, list[str]] = {}
    for node in nodes:
        groups_map.setdefault(find(node.id), []).append(node.id)
    return [ids for ids in groups_map.values() if len(ids) > 1]


def _node_reachable_platforms(
    node: Node,
    group: list[Node],
    registry: Registry,
    platforms: dict[str, list[str]],
) -> set[str]:
    """alter 组内全部成员的组件可达平台并集。"""
    universe = all_platforms(platforms)
    reachable: set[str] = set()
    for member in group:
        comp = registry.get(member.component)
        if comp is None:
            continue
        if not comp.platforms:
            reachable.update(universe)
        else:
            for tag in comp.platforms:
                reachable.update(_descendants(tag, platforms) & universe)
    return reachable


def _resolve_graph(
    graph: Graph,
    platform: str,
    registry: Registry,
    platforms: dict[str, list[str]],
) -> tuple[dict[str, str], dict[str, str], list[str]]:
    """返回 (激活映射 anchor->active, 组映射 anchor->members, 警告)。"""
    groups = _build_groups(graph.nodes.values(), graph)
    active_map: dict[str, str] = {}
    group_map: dict[str, list[str]] = {}
    warnings: list[str] = []

    wired: dict[str, int] = {}
    for conn in graph.connections:
        wired[conn.from_ref.node_id] = wired.get(conn.from_ref.node_id, 0) + 1
        wired[conn.to_ref.node_id] = wired.get(conn.to_ref.node_id, 0) + 1

    for group_ids in groups:
        members = [graph.nodes[i] for i in group_ids]
        check_group_compliance(members, registry)
        anchor = members[0].id
        # 有连线的成员视为锚定（只能有一个被连线）
        wired_members = [m.id for m in members if wired.get(m.id, 0) > 0]
        if len(wired_members) > 1:
            raise ResolutionError(
                f"alter 组 {group_ids} 有多个成员参与连线（{wired_members}）；"
                f"同一槽位只能连一个，其余作为替代候选"
            )
        if wired_members:
            anchor = wired_members[0]
        group_map[anchor] = group_ids

        # 选定平台下激活：锚定支持则用锚定，否则按声明顺序取支持者
        active = None
        ordered = [anchor] + [i for i in group_ids if i != anchor]
        for nid in ordered:
            comp = registry.get(graph.nodes[nid].component)
            if comp is not None and component_supports(comp, platform, platforms):
                active = nid
                break
        if active is None:
            raise ResolutionError(
                f"alter 组 {group_ids} 没有任何成员支持平台 {platform}"
            )
        if active != anchor:
            warnings.append(
                f"alter 组 {group_ids}: 平台 {platform} 下使用 {active} 替代 {anchor}"
            )
        active_map[anchor] = active
    return active_map, group_map, warnings


def resolve_project(
    project: Project,
    registry: Registry,
    target: str | None = None,
) -> tuple[Project, Resolution]:
    """按目标平台解析工程：返回（解析后的工程副本, Resolution 元数据）。

    - 未激活的 alter 成员从解析结果中移除（不参与编译，宏语义）；
    - 锚定成员的连线重映射到激活成员；
    - 工程无 alter 时返回等价副本，平台取 target 或 auto（win 优先）。
    """
    platforms = load_platforms()
    chosen = (target or project.target or "auto").strip().lower()

    # 1) 每节点可达平台（主图 + 全部子组件图）
    all_graphs: list[tuple[str, Graph]] = [("主图", project.graph)]
    all_graphs += [(f"子组件 {s.id}", s.graph) for s in project.subcomponents]
    node_platforms: dict[str, set[str]] = {}
    node_owner: dict[str, str] = {}
    for label, graph in all_graphs:
        groups = _build_groups(graph.nodes.values(), graph)
        for group_ids in groups:
            members = [graph.nodes[i] for i in group_ids]
            check_group_compliance(members, registry)
            reach = _node_reachable_platforms(members[0], members, registry, platforms)
            for nid in group_ids:
                node_platforms[nid] = reach
                node_owner[nid] = label
        for nid, node in graph.nodes.items():
            if nid in node_platforms:
                continue
            comp = registry.get(node.component)
            if comp is None:
                raise ResolutionError(
                    f"组件不存在: {node.component} (node {nid})"
                )
            if comp.manifest.get("execution", {}).get("none"):
                continue  # 声明式平台节点（如 platform_hook）不约束平台可达性
            node_platforms[nid] = _node_reachable_platforms(
                node, [node], registry, platforms
            )
            node_owner[nid] = label

    universe = all_platforms(platforms)
    p_all: set[str] | None = None
    for reach in node_platforms.values():
        p_all = reach if p_all is None else (p_all & reach)
    if p_all is None:
        p_all = set(universe)

    if not p_all:
        detail = "; ".join(
            f"{nid}（{node_owner[nid]}）可达 {sorted(reach)}"
            for nid, reach in sorted(node_platforms.items())
        )
        raise ResolutionError(
            f"模型无统一平台可用：整链无法凑成一个平台。各节点可达平台：{detail}"
        )

    # 2) 选平台
    if chosen in ("auto", ""):
        platform = "win" if "win" in p_all else next(iter(sorted(p_all)))
    else:
        if chosen not in p_all:
            broken = [
                f"{nid}（{node_owner[nid]}）"
                for nid, reach in sorted(node_platforms.items())
                if chosen not in reach
            ]
            raise ResolutionError(
                f"期望目标平台 {chosen} 不可达（模型可用: {sorted(p_all)}）；"
                f"不支持该平台的节点: {broken}"
            )
        platform = chosen

    # 3) 逐图解析：激活成员 + 边重映射
    resolved = copy.deepcopy(project)
    resolved.target = platform
    warnings: list[str] = []
    group_map_all: dict[str, list[str]] = {}
    active_all: dict[str, str] = {}

    for gi, graph in enumerate([resolved.graph] + [s.graph for s in resolved.subcomponents]):
        active_map, group_map, warns = _resolve_graph(
            graph, platform, registry, platforms
        )
        group_map_all.update(group_map)
        active_all.update(active_map)
        warnings.extend(warns)

        # 移除未激活成员
        inactive = {
            nid
            for anchor, members in group_map.items()
            for nid in members
            if nid != active_map[anchor]
        }
        for nid in inactive:
            graph.nodes.pop(nid, None)
        # 边重映射：锚定成员 -> 激活成员
        for conn in graph.connections:
            if conn.from_ref.node_id in active_map:
                conn.from_ref.node_id = active_map[conn.from_ref.node_id]
            if conn.to_ref.node_id in active_map:
                conn.to_ref.node_id = active_map[conn.to_ref.node_id]
        # 控制连接（顶层段，引用主图节点）同样重映射到激活成员
        if gi == 0:
            for cc in resolved.control_connections:
                if cc.from_ref.node_id in active_map:
                    cc.from_ref.node_id = active_map[cc.from_ref.node_id]
                if cc.to_ref.node_id in active_map:
                    cc.to_ref.node_id = active_map[cc.to_ref.node_id]

    return resolved, Resolution(
        platform=platform,
        groups=group_map_all,
        active=active_all,
        warnings=warnings,
    )
