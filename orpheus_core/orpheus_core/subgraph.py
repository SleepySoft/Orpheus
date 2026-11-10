"""Subcomponent (composite) expansion: flatten hierarchical graphs before compile.

Composite components are referenced by the same component ids as atomic
components. Flattening expands every composite instance recursively
into a pure-atomic graph, so the compiler / runtime / codegen never see the
hierarchy (Simulink "virtual subsystem" style).
"""

from __future__ import annotations

import copy
from dataclasses import replace

from orpheus_core.compiler import CompileError
from orpheus_core.project import Connection, ControlConnection, DriveConnection, Graph, Node, PortRef, Project, Subcomponent
from orpheus_core.registry import Registry

SUB_PREFIX = "sub:"
NODE_SEP = "__"
BRIDGE_COMPONENTS = {
    "orpheus.builtin.access_bridge",
    "orpheus.builtin.uart_link",
}


def is_subcomponent_ref(component: str, subs: dict[str, Subcomponent] | None = None) -> bool:
    return component.startswith(SUB_PREFIX) or bool(subs and component in subs)


def subcomponent_id(component: str) -> str:
    """Return the logical id for legacy ``sub:id`` or a uniform bare id."""
    return component[len(SUB_PREFIX):] if component.startswith(SUB_PREFIX) else component


def _validate_subcomponent(sub: Subcomponent, subs: dict[str, Subcomponent]) -> None:
    bridge_nodes = [node.id for node in sub.graph.nodes.values() if node.component in BRIDGE_COMPONENTS]
    if bridge_nodes:
        raise CompileError(
            f"subcomponent {sub.id}: Bridge 配置只能位于工程主图：{sorted(bridge_nodes)}"
        )
    seen: set[str] = set()
    for port in sub.ports:
        if port.propagate_drive is not None and type(port.propagate_drive) is not bool:
            raise CompileError(f"复合组件 {sub.id} 端口 {port.id}：propagate_drive 必须为布尔值")
        if port.id in seen:
            raise CompileError(f"subcomponent {sub.id}: duplicate port id {port.id!r}")
        seen.add(port.id)
        try:
            ref = PortRef.parse(port.maps_to)
        except ValueError as exc:
            raise CompileError(
                f"subcomponent {sub.id}: invalid maps_to {port.maps_to!r}: {exc}"
            ) from exc
        node = sub.graph.nodes.get(ref.node_id)
        if node is None:
            raise CompileError(
                f"subcomponent {sub.id}: port {port.id!r} maps to unknown node {ref.node_id!r}"
            )
    seen_params: set[str] = set()
    for param in sub.public_parameters:
        if param.id in seen_params:
            raise CompileError(f"subcomponent {sub.id}: duplicate public parameter id {param.id!r}")
        seen_params.add(param.id)
        if param.direction not in ("input", "output"):
            raise CompileError(
                f"subcomponent {sub.id}: public parameter {param.id!r} has invalid direction {param.direction!r}"
            )
        try:
            ref = PortRef.parse(param.maps_to)
        except ValueError as exc:
            raise CompileError(
                f"subcomponent {sub.id}: invalid public parameter maps_to {param.maps_to!r}: {exc}"
            ) from exc
        node = sub.graph.nodes.get(ref.node_id)
        if node is None:
            raise CompileError(
                f"subcomponent {sub.id}: public parameter {param.id!r} maps to unknown node {ref.node_id!r}"
            )


def _resolve_boundary_port(
    sub: Subcomponent,
    port_id: str,
    subs: dict[str, Subcomponent],
    stack: tuple[str, ...] = (),
) -> PortRef:
    port = next((candidate for candidate in sub.ports if candidate.id == port_id), None)
    if port is None:
        raise CompileError(f"component {sub.id!r} has no port {port_id!r}")
    target = PortRef.parse(port.maps_to)
    node = sub.graph.nodes[target.node_id]
    if not is_subcomponent_ref(node.component, subs):
        return target
    child_id = subcomponent_id(node.component)
    if child_id in stack:
        raise CompileError("component cycle: " + " -> ".join((*stack, child_id)))
    child = subs.get(child_id)
    if child is None:
        raise CompileError(f"undefined composite component: {node.component!r}")
    nested = _resolve_boundary_port(child, target.port_id, subs, (*stack, sub.id))
    return PortRef(node_id=f"{target.node_id}{NODE_SEP}{nested.node_id}", port_id=nested.port_id)


def _resolve_public_parameter(
    sub: Subcomponent,
    parameter_id: str,
    subs: dict[str, Subcomponent],
    stack: tuple[str, ...] = (),
) -> tuple[Subcomponent, object, PortRef]:
    public = next(
        (candidate for candidate in sub.public_parameters if candidate.id == parameter_id),
        None,
    )
    if public is None:
        raise CompileError(f"component {sub.id!r} has no public parameter {parameter_id!r}")
    target = PortRef.parse(public.maps_to)
    node = sub.graph.nodes[target.node_id]
    if not is_subcomponent_ref(node.component, subs):
        return sub, public, target
    child_id = subcomponent_id(node.component)
    if child_id in stack:
        raise CompileError("component cycle: " + " -> ".join((*stack, child_id)))
    child = subs.get(child_id)
    if child is None:
        raise CompileError(f"undefined composite component: {node.component!r}")
    _, leaf_public, nested = _resolve_public_parameter(
        child, target.port_id, subs, (*stack, sub.id)
    )
    return child, leaf_public, PortRef(
        node_id=f"{target.node_id}{NODE_SEP}{nested.node_id}", port_id=nested.port_id
    )


def instantiate_subgraph(node: Node, sub: Subcomponent) -> Graph:
    """在直接子实例上注入公开初始化值，递归展开和定义预览共用。"""
    output_overrides = [
        public.id for public in sub.public_parameters
        if public.direction == "output" and public.id in node.params
    ]
    if output_overrides:
        raise CompileError(f"node {node.id}: 子组件控制输出不可由实例赋值: {output_overrides}")
    graph = copy.deepcopy(sub.graph)
    for public in sub.public_parameters:
        if public.direction != "input":
            continue
        value = node.params.get(public.id, public.default)
        if value is None:
            continue
        target = PortRef.parse(public.maps_to)
        if target.node_id not in graph.nodes:
            raise CompileError(f"公开参数 {sub.id}:{public.id} 引用不存在的节点 {target.node_id}")
        graph.nodes[target.node_id].params[target.port_id] = copy.deepcopy(value)
    return graph


def _expand_graph(
    graph: Graph,
    subs: dict[str, Subcomponent],
    prefix: str,
    stack: tuple[str, ...],
    context: Project,
    registry: Registry | None,
) -> Graph:
    """Return a flattened copy of `graph` with all node ids prefixed."""
    flat = Graph()
    drive_boundaries: dict[str, tuple[list[str], list[str]]] = {}

    for node in graph.nodes.values():
        if not is_subcomponent_ref(node.component, subs):
            cloned = copy.deepcopy(node)
            cloned.id = prefix + node.id
            flat.nodes[cloned.id] = cloned
            continue

        sub_id = subcomponent_id(node.component)
        sub = subs.get(sub_id)
        if sub is None:
            raise CompileError(f"undefined subcomponent: {node.component!r} (node {node.id})")
        if sub_id in stack:
            cycle = " -> ".join((*stack, sub_id))
            raise CompileError(f"subcomponent cycle: {cycle}")
        inner_source = instantiate_subgraph(node, sub)
        inner = _expand_graph(inner_source, subs, f"{prefix}{node.id}{NODE_SEP}", (*stack, sub_id),
                              context, registry)
        for port in sub.ports:
            target = _resolve_boundary_port(sub, port.id, subs)
            context.interface_bindings.append({
                "origin": f"{prefix}{node.id}:{port.id}",
                "endpoint": f"{prefix}{node.id}{NODE_SEP}{target}",
                "direction": port.direction,
                "signature": port.signature,
                "node": replace(node, params={
                    **{p.id: p.default for p in sub.public_parameters if p.default is not None},
                    **node.params,
                }),
            })
        flat.nodes.update(inner.nodes)
        flat.connections.extend(inner.connections)
        flat.drive_connections.extend(inner.drive_connections)
        flat.drive_barriers.update(inner.drive_barriers)
        boundary_connections = inner.drive_connections
        if context.flow_mode == "task_sequence" and registry is not None:
            from orpheus_core.task_flow import resolve_drive_connections
            boundary_connections = resolve_drive_connections(
                replace(context, graph=inner, drive_connections=inner.drive_connections), registry,
            )
        elif context.flow_mode == "task_sequence" and inner.connections and not boundary_connections:
            raise CompileError("自动推导复合组件驱动入口需要组件 Registry；请传入 flatten_project(project, registry)")
        called = {c.to_ref.node_id for c in inner.connections + inner.drive_connections}
        callers = {c.from_ref.node_id for c in boundary_connections}
        input_entries = [
            f"{prefix}{node.id}{NODE_SEP}{_resolve_boundary_port(sub, port.id, subs).node_id}"
            for port in sub.ports if port.direction == "input"
        ]
        drive_boundaries[node.id] = (
            list(dict.fromkeys(input_entries)) if input_entries else
            [nid for nid in inner.nodes if nid not in called],
            [nid for nid in inner.nodes if nid not in callers],
        )

    def map_endpoint(ref: PortRef) -> PortRef:
        node = graph.nodes.get(ref.node_id)
        if node is None or not is_subcomponent_ref(node.component, subs):
            return PortRef(node_id=prefix + ref.node_id, port_id=ref.port_id)
        sub = subs[subcomponent_id(node.component)]
        inner = _resolve_boundary_port(sub, ref.port_id, subs)
        return PortRef(
            node_id=f"{prefix}{ref.node_id}{NODE_SEP}{inner.node_id}",
            port_id=inner.port_id,
        )

    def boundary_blocks(ref: PortRef) -> bool:
        node = graph.nodes.get(ref.node_id)
        if node is None or not is_subcomponent_ref(node.component, subs):
            return False
        sub = subs[subcomponent_id(node.component)]
        port = next(p for p in sub.ports if p.id == ref.port_id)
        return port.propagate_drive is False or boundary_blocks_in_sub(sub, port.maps_to)

    def boundary_blocks_in_sub(sub: Subcomponent, endpoint: str) -> bool:
        ref = PortRef.parse(endpoint)
        child_node = sub.graph.nodes[ref.node_id]
        if not is_subcomponent_ref(child_node.component, subs):
            return False
        child = subs[subcomponent_id(child_node.component)]
        port = next(p for p in child.ports if p.id == ref.port_id)
        return port.propagate_drive is False or boundary_blocks_in_sub(child, port.maps_to)

    for conn in graph.connections:
        mapped = Connection(from_ref=map_endpoint(conn.from_ref), to_ref=map_endpoint(conn.to_ref))
        flat.connections.append(mapped)
        if (boundary_blocks(conn.from_ref) or boundary_blocks(conn.to_ref)
                or (str(conn.from_ref), str(conn.to_ref)) in graph.drive_barriers):
            flat.drive_barriers.add((str(mapped.from_ref), str(mapped.to_ref)))

    for conn in graph.drive_connections:
        source = conn.from_ref.node_id
        target = conn.to_ref.node_id
        if conn.from_ref.port_id != "out" or conn.to_ref.port_id != "in":
            raise CompileError("驱动接口必须使用 node:out → node:in")
        sources = drive_boundaries[source][1] if source in drive_boundaries else [prefix + source]
        targets = drive_boundaries[target][0] if target in drive_boundaries else [prefix + target]
        if not sources or not targets:
            raise CompileError(f"子组件驱动边界为空或存在循环：{source} → {target}")
        for src in sources:
            for dst in targets:
                flat.drive_connections.append(DriveConnection(
                    PortRef(src, "out"), PortRef(dst, "in"), conn.every, conn.phase,
                ))

    return flat


def flatten_project(project: Project, registry: Registry | None = None) -> Project:
    """Return a new Project whose graph contains only atomic components."""
    subs = {s.id: s for s in project.subcomponents}
    for sub in subs.values():
        _validate_subcomponent(sub, subs)

    def map_control_endpoint(ref: PortRef, expected_direction: str) -> PortRef:
        node = project.graph.nodes.get(ref.node_id)
        if node is None or not is_subcomponent_ref(node.component, subs):
            return copy.deepcopy(ref)
        sub = subs[subcomponent_id(node.component)]
        _, public, target = _resolve_public_parameter(sub, ref.port_id, subs)
        if public.direction != expected_direction:
            role = "源" if expected_direction == "output" else "目标"
            raise CompileError(
                f"控制连接{role} {ref} 的公开参数方向应为 {expected_direction}，"
                f"实际为 {public.direction}"
            )
        return PortRef(node_id=f"{node.id}{NODE_SEP}{target.node_id}", port_id=target.port_id)

    flat_control: list[ControlConnection] = []
    for cc in project.control_connections:
        flat_control.append(ControlConnection(
            from_ref=map_control_endpoint(cc.from_ref, "output"),
            to_ref=map_control_endpoint(cc.to_ref, "input"),
        ))

    flat = copy.copy(project)  # shallow: tasks/metadata shared, graph replaced
    graph = copy.copy(project.graph)
    graph.drive_connections = project.drive_connections
    flat.interface_bindings = []
    flat.graph = _expand_graph(graph, subs, prefix="", stack=(),
                               context=flat, registry=registry or project.registry)
    flat.drive_connections = flat.graph.drive_connections
    flat.graph.drive_connections = []
    flat.bridges = copy.deepcopy(project.bridges)
    flat.subcomponents = []
    flat.control_connections = flat_control
    return flat
