"""Subcomponent (composite) expansion: flatten hierarchical graphs before compile.

Subcomponents are project-private composites referenced by nodes with
``component: "sub:<sub_id>"``. Flattening expands every instance recursively
into a pure-atomic graph, so the compiler / runtime / codegen never see the
hierarchy (Simulink "virtual subsystem" style).

v1 restrictions:
- ``maps_to`` of a sub port must reference an *atomic* internal node port.
- No parameter promotion (mask parameters) on instances.
"""

from __future__ import annotations

import copy

from orpheus_core.compiler import CompileError
from orpheus_core.project import Connection, ControlConnection, Graph, Node, PortRef, Project, Subcomponent

SUB_PREFIX = "sub:"
NODE_SEP = "__"


def is_subcomponent_ref(component: str) -> bool:
    return component.startswith(SUB_PREFIX)


def subcomponent_id(component: str) -> str:
    """'sub:accumulator' -> 'accumulator'."""
    return component[len(SUB_PREFIX):]


def _validate_subcomponent(sub: Subcomponent) -> None:
    seen: set[str] = set()
    for port in sub.ports:
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
        if is_subcomponent_ref(node.component):
            raise CompileError(
                f"subcomponent {sub.id}: port {port.id!r} maps to nested subcomponent "
                f"instance {ref.node_id!r}; map to an atomic node instead"
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
        if is_subcomponent_ref(node.component):
            raise CompileError(
                f"subcomponent {sub.id}: public parameter {param.id!r} maps to nested subcomponent "
                f"instance {ref.node_id!r}; map to an atomic node instead"
            )


def _expand_graph(
    graph: Graph,
    subs: dict[str, Subcomponent],
    prefix: str,
    stack: tuple[str, ...],
) -> Graph:
    """Return a flattened copy of `graph` with all node ids prefixed."""
    flat = Graph()

    for node in graph.nodes.values():
        if not is_subcomponent_ref(node.component):
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
        output_overrides = [
            public.id for public in sub.public_parameters
            if public.direction == "output" and public.id in node.params
        ]
        if output_overrides:
            raise CompileError(
                f"node {node.id}: 子组件控制输出不可由实例赋值: {output_overrides}"
            )
        inner = _expand_graph(sub.graph, subs, f"{prefix}{node.id}{NODE_SEP}", (*stack, sub_id))
        for public in sub.public_parameters:
            if public.direction != "input":
                continue
            value = node.params.get(public.id, public.default)
            if value is None:
                continue
            target = PortRef.parse(public.maps_to)
            flat_target = f"{prefix}{node.id}{NODE_SEP}{target.node_id}"
            inner.nodes[flat_target].params[target.port_id] = copy.deepcopy(value)
        flat.nodes.update(inner.nodes)
        flat.connections.extend(inner.connections)

    def map_endpoint(ref: PortRef) -> PortRef:
        node = graph.nodes.get(ref.node_id)
        if node is None or not is_subcomponent_ref(node.component):
            return PortRef(node_id=prefix + ref.node_id, port_id=ref.port_id)
        sub = subs[subcomponent_id(node.component)]
        for port in sub.ports:
            if port.id == ref.port_id:
                inner = PortRef.parse(port.maps_to)
                return PortRef(
                    node_id=f"{prefix}{ref.node_id}{NODE_SEP}{inner.node_id}",
                    port_id=inner.port_id,
                )
        raise CompileError(
            f"node {ref.node_id}: subcomponent {node.component!r} has no port {ref.port_id!r}"
        )

    for conn in graph.connections:
        flat.connections.append(
            Connection(from_ref=map_endpoint(conn.from_ref), to_ref=map_endpoint(conn.to_ref))
        )

    return flat


def flatten_project(project: Project) -> Project:
    """Return a new Project whose graph contains only atomic components."""
    subs = {s.id: s for s in project.subcomponents}
    for sub in subs.values():
        _validate_subcomponent(sub)

    def map_control_endpoint(ref: PortRef, expected_direction: str) -> PortRef:
        node = project.graph.nodes.get(ref.node_id)
        if node is None or not is_subcomponent_ref(node.component):
            return copy.deepcopy(ref)
        sub = subs[subcomponent_id(node.component)]
        public = next((p for p in sub.public_parameters if p.id == ref.port_id), None)
        if public is None:
            raise CompileError(
                f"节点 {node.id}: 子组件 {node.component!r} 没有公开参数 {ref.port_id!r}"
            )
        if public.direction != expected_direction:
            role = "源" if expected_direction == "output" else "目标"
            raise CompileError(
                f"控制连接{role} {ref} 的公开参数方向应为 {expected_direction}，"
                f"实际为 {public.direction}"
            )
        target = PortRef.parse(public.maps_to)
        return PortRef(node_id=f"{node.id}{NODE_SEP}{target.node_id}", port_id=target.port_id)

    flat_control: list[ControlConnection] = []
    for cc in project.control_connections:
        flat_control.append(ControlConnection(
            from_ref=map_control_endpoint(cc.from_ref, "output"),
            to_ref=map_control_endpoint(cc.to_ref, "input"),
        ))

    flat = copy.copy(project)  # shallow: tasks/metadata shared, graph replaced
    flat.graph = _expand_graph(project.graph, subs, prefix="", stack=())
    flat.subcomponents = []
    flat.control_connections = flat_control
    return flat
