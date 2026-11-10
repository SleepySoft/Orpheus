"""编辑态端口预览：沿公开引用展开，签名只由编译器推导，不写回声明。"""

from __future__ import annotations

import copy
from dataclasses import replace
from typing import Any

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import Graph, Node, Project, Subcomponent
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project, instantiate_subgraph, subcomponent_id


def _groups(graph: Graph) -> list[set[str]]:
    adjacent = {node: set() for node in graph.nodes}
    for edge in graph.connections:
        source, target = edge.from_ref.node_id, edge.to_ref.node_id
        if source in adjacent and target in adjacent:
            adjacent[source].add(target)
            adjacent[target].add(source)
    groups = []
    remaining = set(adjacent)
    while remaining:
        group: set[str] = set()
        pending = [min(remaining)]
        while pending:
            node = pending.pop()
            if node in group:
                continue
            group.add(node)
            pending.extend(adjacent[node] - group)
        remaining -= group
        groups.append(group)
    return groups


def _definitions(graph: Graph, subs: dict[str, Subcomponent]) -> list[Subcomponent]:
    needed: dict[str, Subcomponent] = {}
    pending = list(graph.nodes.values())
    while pending:
        component = subcomponent_id(pending.pop().component)
        if component in subs and component not in needed:
            needed[component] = subs[component]
            pending.extend(subs[component].graph.nodes.values())
    return list(needed.values())


def preview_interfaces(project: Project, registry: Registry, view_key: str = "main") -> dict[str, Any]:
    """按数据连通分量报告错误；未连接时钟不妨碍查看声明接口。"""
    subs = {sub.id: sub for sub in project.subcomponents}
    graph = project.graph
    if view_key != "main":
        sub = subs.get(subcomponent_id(view_key))
        if sub is None:
            raise CompileError(f"不存在的组件视图：{view_key}")
        graph = instantiate_subgraph(Node(id=sub.id, component=sub.id), sub)
    compiler = GraphCompiler(registry)
    nodes: dict[str, Any] = {}
    diagnostics = []
    connections = []
    for edge in graph.connections:
        if edge.from_ref.node_id not in graph.nodes and edge.to_ref.node_id not in graph.nodes:
            diagnostics.append({"nodes": [], "message": f"连接引用不存在的节点：{edge.from_ref} → {edge.to_ref}"})
    parts: list[tuple[set[str], Graph, Project]] = []

    def report_error(group: set[str], message: str) -> None:
        diagnostics.append({"nodes": sorted(group), "message": message})
        for key in group:
            nodes[key] = {"status": "error", "message": message}
        connections.extend({
            "from": str(edge.from_ref), "to": str(edge.to_ref),
            "status": "error", "differences": [], "message": message,
        } for edge in graph.connections
            if edge.from_ref.node_id in group or edge.to_ref.node_id in group)

    for group in _groups(graph):
        part = Graph(
            nodes={key: copy.deepcopy(graph.nodes[key]) for key in graph.nodes if key in group},
            connections=[edge for edge in graph.connections
                         if edge.from_ref.node_id in group or edge.to_ref.node_id in group],
        )
        snapshot = replace(
            project, graph=part, subcomponents=_definitions(part, subs),
            control_connections=[], drive_connections=[], flow_mode="dataflow",
        )
        try:
            flat = flatten_project(snapshot, registry)
            parts.append((group, part, flat))
        except (CompileError, ValueError) as exc:
            report_error(group, str(exc))

    clock_context = replace(project, graph=Graph(nodes={
        key: node for _, _, flat in parts for key, node in flat.graph.nodes.items()
    }))
    tasks = {}
    task_errors = {}
    for task in list(project.tasks.values()) or [project.get_default_task()]:
        try:
            tasks[task.id] = compiler._resolve_source_rate(clock_context, task)
        except CompileError as exc:
            task_errors[task.id] = str(exc)
    for group, part, flat in parts:
        try:
            for node in flat.graph.nodes.values():
                if node.task in task_errors:
                    raise CompileError(task_errors[node.task])
            flat = replace(flat, tasks=tasks)
            conflicts: list[dict[str, Any]] = []
            edge_diagnostics: list[dict[str, Any]] = []
            signatures, expanded = compiler.resolve_port_signatures(
                flat, diagnostics=conflicts, connection_diagnostics=edge_diagnostics)
            diagnostics.extend(conflicts)
            bindings = {binding["origin"]: binding["endpoint"] for binding in flat.interface_bindings}
            for key, node in part.nodes.items():
                sub = subs.get(subcomponent_id(node.component))
                declarations = (
                    [{"id": port.id, "direction": port.direction, "maps_to": port.maps_to,
                      **({"propagate_drive": port.propagate_drive}
                         if port.propagate_drive is not None else {})}
                     for port in sub.ports]
                    if sub else expanded[key]
                )
                ports = []
                for declaration in declarations:
                    endpoint = bindings.get(f"{key}:{declaration['id']}", f"{key}:{declaration['id']}")
                    leaf = flat.graph.nodes[endpoint.split(":")[0]]
                    messages = [item["message"] for item in conflicts if item["endpoint"] == endpoint]
                    ports.append({
                        **declaration, **signatures[endpoint],
                        "block_size": signatures[endpoint]["frames"],
                        "resolution": {"status": "conflict" if messages else "resolved",
                                       "message": "；".join(messages),
                                       "endpoint": endpoint, "component": leaf.component},
                    })
                nodes[key] = {
                    "status": "resolved", "ports": ports,
                    "diagnostics": [item for item in conflicts if item["origin"].startswith(
                        (f"{key}:", f"{key}__"),
                    )],
                }
            by_edge = {(edge["from"], edge["to"]): edge for edge in edge_diagnostics}
            for edge in part.connections:
                source, target = str(edge.from_ref), str(edge.to_ref)
                resolved = by_edge[(bindings.get(source, source), bindings.get(target, target))]
                constraints = [item["message"] for item in conflicts if item["origin"] in (source, target)]
                message = "\n".join([resolved["message"], *constraints]).strip()
                connection = {
                    **resolved, "from": source, "to": target,
                    "status": "mismatch" if resolved["differences"] or constraints else "matched",
                    "message": message,
                }
                connections.append(connection)
                if message:
                    diagnostics.append({"nodes": [edge.from_ref.node_id, edge.to_ref.node_id],
                                        "from": source, "to": target, "message": message})
        except (CompileError, ValueError, TypeError, ZeroDivisionError) as exc:
            report_error(group, str(exc))
    return {"view_key": view_key, "nodes": nodes, "connections": connections, "diagnostics": diagnostics}
