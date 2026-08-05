"""Graph compiler: parse, type-check, derive signatures, schedule."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import ComponentInfo, Registry


class CompileError(Exception):
    pass


@dataclass
class ResolvedPort:
    node_id: str
    port_id: str
    direction: str
    type: str
    sample_format: str
    channels: int
    sample_rate: int
    block_size: int


@dataclass
class ExecutionPlan:
    abi_version: int
    sample_rate: int
    block_size: int
    task_id: str
    nodes: list[str] = field(default_factory=list)
    execution_order: list[str] = field(default_factory=list)
    node_configs: dict[str, dict[str, Any]] = field(default_factory=dict)
    buffers: dict[str, dict[str, Any]] = field(default_factory=dict)
    connections: list[dict[str, str]] = field(default_factory=list)


def _resolve_value(expr: Any, node: Node, task: Task) -> Any:
    """Resolve a manifest expression against node params and task context."""
    if isinstance(expr, str) and expr.startswith("param:"):
        param_id = expr[6:]
        return node.params.get(param_id)
    if expr == "task:sample_rate":
        return task.sample_rate
    if expr == "task:block_size":
        return task.block_size
    return expr


def _expand_port_manifests(
    node: Node, comp: ComponentInfo, task: Task
) -> list[dict[str, Any]]:
    """Expand a component's port manifests for a node.

    A port entry may declare ``count: <expr>`` (e.g. ``param:channels``) to be
    replicated N times with concrete ids ``<id>0..<id>N-1`` (variable pins,
    e.g. deinterleave outputs).
    """
    expanded: list[dict[str, Any]] = []
    for pm in comp.manifest.get("ports", []):
        count_expr = pm.get("count")
        if count_expr is None:
            expanded.append(pm)
            continue
        count = _resolve_value(count_expr, node, task)
        if count is None and isinstance(count_expr, str) and count_expr.startswith("param:"):
            param_id = count_expr[6:]
            for p in comp.manifest.get("parameters", []):
                if p["id"] == param_id:
                    count = p.get("default")
                    break
        try:
            count = int(count)
        except (TypeError, ValueError) as exc:
            raise CompileError(
                f"cannot resolve port count for {node.id}:{pm['id']} ({count_expr!r})"
            ) from exc
        if count < 1:
            raise CompileError(f"invalid port count {count} for {node.id}:{pm['id']}")
        for i in range(count):
            replica = {k: v for k, v in pm.items() if k != "count"}
            replica["id"] = f"{pm['id']}{i}"
            expanded.append(replica)
    return expanded


def _resolve_port_signature(
    node: Node,
    comp: ComponentInfo,
    port_manifest: dict[str, Any],
    task: Task,
) -> ResolvedPort:
    direction = port_manifest["direction"]
    port_type = port_manifest.get("type", "audio")
    sample_format = port_manifest.get("sample_format", "f32")

    channels_expr = port_manifest.get("channels", task.block_size)
    channels = _resolve_value(channels_expr, node, task)
    if channels is None:
        # 尝试从参数默认值读取
        param_id = port_manifest.get("channels_param")
        if param_id:
            for p in comp.manifest.get("parameters", []):
                if p["id"] == param_id:
                    channels = p.get("default")
                    break
    if channels is None:
        raise CompileError(f"cannot resolve channels for {node.id}:{port_manifest['id']}")

    sample_rate_expr = port_manifest.get("sample_rate", "task:sample_rate")
    sample_rate = _resolve_value(sample_rate_expr, node, task)
    if sample_rate is None:
        sample_rate = task.sample_rate

    block_size_expr = port_manifest.get("block_size", "task:block_size")
    block_size = _resolve_value(block_size_expr, node, task)
    if block_size is None:
        block_size = task.block_size

    return ResolvedPort(
        node_id=node.id,
        port_id=port_manifest["id"],
        direction=direction,
        type=port_type,
        sample_format=sample_format,
        channels=int(channels),
        sample_rate=int(sample_rate),
        block_size=int(block_size),
    )


class GraphCompiler:
    def __init__(self, registry: Registry):
        self.registry = registry

    def compile(self, project: Project) -> ExecutionPlan:
        graph = project.graph
        task = project.get_default_task()

        # 1. Resolve nodes and port signatures (expanding variable-count ports)
        resolved_ports: dict[str, ResolvedPort] = {}
        expanded_ports: dict[str, list[dict[str, Any]]] = {}
        for node in graph.nodes.values():
            comp = self.registry.get(node.component)
            if comp is None:
                raise CompileError(f"component not found: {node.component} (node {node.id})")
            port_manifests = _expand_port_manifests(node, comp, task)
            expanded_ports[node.id] = port_manifests
            for port_manifest in port_manifests:
                key = f"{node.id}:{port_manifest['id']}"
                resolved_ports[key] = _resolve_port_signature(node, comp, port_manifest, task)

        # 2. Validate connections
        for conn in graph.connections:
            from_key = str(conn.from_ref)
            to_key = str(conn.to_ref)
            from_port = resolved_ports.get(from_key)
            to_port = resolved_ports.get(to_key)
            if from_port is None:
                raise CompileError(f"unknown source port: {from_key}")
            if to_port is None:
                raise CompileError(f"unknown target port: {to_key}")
            if from_port.direction != "output":
                raise CompileError(f"source port is not output: {from_key}")
            if to_port.direction != "input":
                raise CompileError(f"target port is not input: {to_key}")
            if from_port.type != to_port.type:
                raise CompileError(
                    f"type mismatch: {from_key} ({from_port.type}) -> {to_key} ({to_port.type})"
                )
            if from_port.sample_format != to_port.sample_format:
                raise CompileError(
                    f"format mismatch: {from_key} ({from_port.sample_format}) -> {to_key} ({to_port.sample_format})"
                )
            if from_port.channels != to_port.channels:
                raise CompileError(
                    f"channels mismatch: {from_key} ({from_port.channels}) -> {to_key} ({to_port.channels})"
                )
            if from_port.sample_rate != to_port.sample_rate:
                raise CompileError(
                    f"sample rate mismatch: {from_key} ({from_port.sample_rate}) -> {to_key} ({to_port.sample_rate})"
                )

        # 3. Topological sort
        execution_order = self._topological_sort(graph)

        # 4. Build execution plan
        plan = ExecutionPlan(
            abi_version=1,
            sample_rate=task.sample_rate,
            block_size=task.block_size,
            task_id=task.id,
            nodes=list(graph.nodes.keys()),
            execution_order=execution_order,
        )

        for node in graph.nodes.values():
            comp = self.registry.get(node.component)
            port_manifests = expanded_ports[node.id]
            plan.node_configs[node.id] = {
                "component": node.component,
                "version": comp.version if comp else "",
                "params": dict(node.params),
                "task": node.task,
                # ordered port ids: runtime binds buffers by port id, not order
                "input_ports": [p["id"] for p in port_manifests if p["direction"] == "input"],
                "output_ports": [p["id"] for p in port_manifests if p["direction"] == "output"],
            }

        # 分配 buffer id：每个连接一个 buffer
        buffer_id = 0
        for conn in graph.connections:
            key = f"buf_{buffer_id}"
            buffer_id += 1
            from_port = resolved_ports[str(conn.from_ref)]
            plan.buffers[key] = {
                "from": str(conn.from_ref),
                "to": str(conn.to_ref),
                "sample_format": from_port.sample_format,
                "channels": from_port.channels,
                "frame_count": from_port.block_size,
            }
            plan.connections.append(
                {
                    "from": str(conn.from_ref),
                    "to": str(conn.to_ref),
                    "buffer": key,
                }
            )

        return plan

    def _topological_sort(self, graph: Graph) -> list[str]:
        in_degree: dict[str, int] = {n: 0 for n in graph.nodes}
        adj: dict[str, list[str]] = {n: [] for n in graph.nodes}
        for conn in graph.connections:
            src = conn.from_ref.node_id
            dst = conn.to_ref.node_id
            if src in adj and dst in in_degree:
                adj[src].append(dst)
                in_degree[dst] += 1

        queue = [n for n, d in in_degree.items() if d == 0]
        order: list[str] = []
        while queue:
            node = queue.pop(0)
            order.append(node)
            for neighbor in adj[node]:
                in_degree[neighbor] -= 1
                if in_degree[neighbor] == 0:
                    queue.append(neighbor)

        if len(order) != len(graph.nodes):
            raise CompileError("graph contains cycle")
        return order
