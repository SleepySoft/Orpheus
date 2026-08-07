"""Graph compiler: parse, type-check, derive signatures, schedule."""

from __future__ import annotations

import re
from dataclasses import dataclass, field, replace
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
    buffer_size: int = 0
    nodes: list[str] = field(default_factory=list)
    execution_order: list[str] = field(default_factory=list)
    node_configs: dict[str, dict[str, Any]] = field(default_factory=dict)
    buffers: dict[str, dict[str, Any]] = field(default_factory=dict)
    connections: list[dict[str, str]] = field(default_factory=list)
    duration_frames: int = 0   # 离线宿主运行时长提示（纯时钟图按扫频 duration_s 推导；0=宿主默认）


def _resolve_atom(expr: Any, node: Node, task: Task) -> Any:
    if isinstance(expr, str) and expr.startswith("param:"):
        param_id = expr[6:]
        return node.params.get(param_id)
    if expr == "task:sample_rate":
        return task.sample_rate
    if expr == "task:block_size":
        return task.block_size
    return expr


def _resolve_value(expr: Any, node: Node, task: Task) -> Any:
    """Resolve a manifest expression against node params and task context.

    Supports integer arithmetic chains over atoms, e.g.
    ``task:block_size*param:factor`` or ``task:sample_rate/param:factor``.
    """
    if isinstance(expr, str) and ("*" in expr or "/" in expr):
        tokens = [t.strip() for t in re.split(r"([*/])", expr)]
        result = _resolve_atom(tokens[0], node, task)
        i = 1
        while i < len(tokens) - 1:
            op = tokens[i]
            operand = _resolve_atom(tokens[i + 1], node, task)
            if result is None or operand is None:
                return None
            if op == "*":
                result = int(result) * int(operand)
            else:
                if int(operand) == 0:
                    raise CompileError(f"division by zero in expression {expr!r} (node {node.id})")
                result = int(result) // int(operand)
            i += 2
        return result
    return _resolve_atom(expr, node, task)


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

    # Device source components that may declare an authoritative sample_rate.
    _DEVICE_SOURCE_COMPONENTS = (
        "orpheus.builtin.device_in",
        "orpheus.builtin.device_out",
    )

    def _resolve_source_rate(self, project: Project, task: Task) -> Task:
        """Adopt a device source's sample_rate as the graph rate when declared.

        block_size stays project-global (it is a graph scheduling quantum, not a
        device property). sample_rate, when a device source declares one, becomes
        the graph's compile-time rate; all device sources must agree. Actual
        device-native validation happens in the realtime host (rt_host).
        """
        rates: set[int] = set()
        for node in project.graph.nodes.values():
            if node.component not in self._DEVICE_SOURCE_COMPONENTS:
                continue
            raw = node.params.get("sample_rate")
            if raw is None or raw == 0 or str(raw).strip() == "":
                continue
            try:
                rates.add(int(raw))
            except (TypeError, ValueError) as exc:
                raise CompileError(
                    f"invalid sample_rate {raw!r} on node {node.id}"
                ) from exc
        if len(rates) > 1:
            raise CompileError(
                f"device sources disagree on sample_rate: {sorted(rates)}"
            )
        if len(rates) == 1:
            return replace(task, sample_rate=next(iter(rates)))
        return task

    def compile(self, project: Project) -> ExecutionPlan:
        graph = project.graph
        task = project.get_default_task()
        task = self._resolve_source_rate(project, task)

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
        driven_inputs: set[str] = set()
        for conn in graph.connections:
            from_key = str(conn.from_ref)
            to_key = str(conn.to_ref)
            if to_key in driven_inputs:
                raise CompileError(f"input port driven by multiple sources: {to_key}")
            driven_inputs.add(to_key)
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

        # 2.5 Clock domains: every flow must be driven by exactly one clock
        self._validate_clock_domains(graph)

        # 3. Topological sort
        execution_order = self._topological_sort(graph)

        # 3.5 Rate divisor propagation (multi-rate scheduling)
        node_divisor = self._propagate_rate_divisors(graph, expanded_ports, execution_order, task)

        # 4. Build execution plan
        plan = ExecutionPlan(
            abi_version=1,
            sample_rate=task.sample_rate,
            block_size=task.block_size,
            buffer_size=project.buffer_size,
            task_id=task.id,
            nodes=list(graph.nodes.keys()),
            execution_order=execution_order,
        )

        # per-node processing quantum: the producer buffer's frame count
        # (differs from task block size in rate-shifted domains)
        in_frames: dict[str, int] = {}
        for conn in graph.connections:
            from_port = resolved_ports[str(conn.from_ref)]
            to_node = conn.to_ref.node_id
            if to_node in in_frames and in_frames[to_node] != from_port.block_size:
                raise CompileError(
                    f"block size mismatch at node {to_node}: inputs differ "
                    f"({in_frames[to_node]} vs {from_port.block_size})"
                )
            in_frames[to_node] = from_port.block_size

        for node in graph.nodes.values():
            comp = self.registry.get(node.component)
            port_manifests = expanded_ports[node.id]
            # effective sample rate: from any resolved port of this node
            node_rate = task.sample_rate
            for pm in port_manifests:
                rp = resolved_ports.get(f"{node.id}:{pm['id']}")
                if rp is not None:
                    node_rate = rp.sample_rate
                    break
            plan.node_configs[node.id] = {
                "component": node.component,
                "version": comp.version if comp else "",
                "params": dict(node.params),
                "task": node.task,
                "divisor": node_divisor[node.id],
                "frames": in_frames.get(node.id, task.block_size),
                "sample_rate": node_rate,
                # ordered port ids: runtime binds buffers by port id, not order
                "input_ports": [p["id"] for p in port_manifests if p["direction"] == "input"],
                "output_ports": [p["id"] for p in port_manifests if p["direction"] == "output"],
            }

        # 离线运行时长：无文件输入时，宿主按纯时钟源（扫频发生器/记录）的时长跑，
        # 否则固定 10s 会截断长扫频（60s 只跑 10s，或跟着 1s 的 wav 只跑 1s）。
        max_dur = 0.0
        for cfg in plan.node_configs.values():
            if cfg["component"] in ("orpheus.builtin.sweep_gen", "orpheus.builtin.sweep_record"):
                try:
                    d = float(cfg["params"].get("duration_s", 0.0) or 0.0)
                except (TypeError, ValueError):
                    d = 0.0
                if d > max_dur:
                    max_dur = d
        if max_dur > 0.0:
            plan.duration_frames = int(max_dur * task.sample_rate + 0.5)

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

    def _validate_clock_domains(self, graph: Graph) -> None:
        """Every connected flow must be driven by exactly one clock domain.

        Clock sources are components tagged `clock_source: true` with a
        `clock_domain` (e.g. device/file). A graph without any clock source
        runs on the implicit host clock (legacy behavior). Otherwise every
        connected component must contain a clock source, and no component may
        mix two strong (non-file) domains.
        """
        parent = {nid: nid for nid in graph.nodes}

        def find(x: str) -> str:
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        for conn in graph.connections:
            a, b = find(conn.from_ref.node_id), find(conn.to_ref.node_id)
            if a != b:
                parent[a] = b

        components: dict[str, list[str]] = {}
        for nid in graph.nodes:
            components.setdefault(find(nid), []).append(nid)

        any_clocked = False
        undriven: list[str] = []
        for members in components.values():
            domains: set[str] = set()
            for nid in members:
                info = self.registry.get(graph.nodes[nid].component)
                manifest = info.manifest if info else {}
                if manifest.get("clock_source"):
                    domains.add(manifest.get("clock_domain", graph.nodes[nid].component))
            strong = domains - {"file"}
            if len(strong) > 1:
                raise CompileError(
                    f"conflicting clock domains {sorted(strong)} in flow {sorted(members)}; "
                    f"separate them or insert an async bridge"
                )
            if domains:
                any_clocked = True
            else:
                undriven.extend(members)
        if any_clocked and undriven:
            raise CompileError(
                f"flow not driven by any clock source (cannot start): {sorted(undriven)}. "
                f"Connect it to a clocked flow or remove it."
            )

    def _propagate_rate_divisors(
        self,
        graph: Graph,
        expanded_ports: dict[str, list[dict[str, Any]]],
        execution_order: list[str],
        task: Task,
    ) -> dict[str, int]:
        """Propagate rate divisors along edges.

        A component may declare ``scheduling.divisor: <expr>`` (e.g. resample):
        it runs at its input rate every block, while its output domain (and all
        downstream nodes) run once every N blocks.
        """
        port_divisor: dict[str, int] = {}
        node_divisor: dict[str, int] = {}
        for node_id in execution_order:
            node = graph.nodes[node_id]
            incoming = [c for c in graph.connections if c.to_ref.node_id == node_id]
            up = {port_divisor[str(c.from_ref)] for c in incoming}
            if len(up) > 1:
                raise CompileError(
                    f"rate mismatch at node {node_id}: input divisors {sorted(up)} "
                    f"(merge different rates via appropriate rate components)"
                )
            d_in = next(iter(up)) if up else 1
            node_divisor[node_id] = d_in
            comp = self.registry.get(node.component)
            factor_expr = (comp.manifest.get("scheduling") or {}).get("divisor") if comp else None
            factor = 1
            if factor_expr is not None:
                resolved = _resolve_value(factor_expr, node, task)
                if resolved is None:
                    raise CompileError(f"cannot resolve rate divisor for node {node_id}")
                factor = int(resolved)
                if factor < 1:
                    raise CompileError(f"invalid rate divisor {factor} for node {node_id}")
            for pm in expanded_ports[node_id]:
                if pm["direction"] == "output":
                    port_divisor[f"{node_id}:{pm['id']}"] = d_in * factor
        return node_divisor

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
