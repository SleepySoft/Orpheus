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
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Subcomponent

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
        inner = _expand_graph(sub.graph, subs, f"{prefix}{node.id}{NODE_SEP}", (*stack, sub_id))
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

    flat = copy.copy(project)  # shallow: tasks/metadata shared, graph replaced
    flat.graph = _expand_graph(project.graph, subs, prefix="", stack=())
    flat.subcomponents = []
    return flat
