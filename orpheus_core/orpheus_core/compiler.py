"""Graph compiler: parse, type-check, derive signatures, schedule."""

from __future__ import annotations

import re
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any

from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import ComponentInfo, Registry
from orpheus_core.parameter_catalog import id_form_of, id_kind_of, id_value


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
    declarations: list[dict[str, Any]] = field(default_factory=list)  # 声明式平台节点（execution.none）
    modules: list[dict[str, Any]] = field(default_factory=list)  # 模块内存布局（ID 寻址：模块 id + 槽）
    id_map: list[dict[str, Any]] = field(default_factory=list)   # 数据点 ID 表（动态/生成两路共用）


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

    def _resolve_source_rate(self, project: Project, task: Task) -> Task:
        """Adopt a clock source's sample_rate as the graph rate when declared.

        block_size stays project-global (it is a graph scheduling quantum, not a
        clock property). 任何声明 clock_source 的组件（设备输入、信号发生器、
        扫频发生器）带有 sample_rate 参数时，该值成为图的编译期采样率；
        多个时钟源必须一致。设备原生的采样率校验在实时宿主（rt_host）完成。
        """
        rates: set[int] = set()
        for node in project.graph.nodes.values():
            info = self.registry.get(node.component)
            if info is None or not info.manifest.get("clock_source"):
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
                f"clock sources disagree on sample_rate: {sorted(rates)}"
            )
        if len(rates) == 1:
            return replace(task, sample_rate=next(iter(rates)))
        return task

    def compile(self, project: Project, target: str | None = None) -> ExecutionPlan:
        # 目标平台与 alter 组解析：先把工程解析为选定平台下的等价副本
        # （替换 alter 成员、剔除未激活成员、重映射边），再走现有编译管线。
        from orpheus_core.resolve import resolve_project

        resolved, _resolution = resolve_project(project, self.registry, target)
        project = resolved
        graph = project.graph
        task = project.get_default_task()
        task = self._resolve_source_rate(project, task)

        # 声明式平台节点（execution.none，如 platform_hook）：不参与执行计划，
        # 仅作为声明进入 plan.declarations，生成器据此产出用户钩子（不连线）。
        declarations: list[dict[str, Any]] = []
        for nid in list(graph.nodes):
            decl_comp = self.registry.get(graph.nodes[nid].component)
            if decl_comp and decl_comp.manifest.get("execution", {}).get("none"):
                for conn in graph.connections:
                    if conn.from_ref.node_id == nid or conn.to_ref.node_id == nid:
                        raise CompileError(
                            f"平台资源节点 {nid}（{graph.nodes[nid].component}）不支持连线"
                        )
                declarations.append(
                    {
                        "id": nid,
                        "component": graph.nodes[nid].component,
                        "params": dict(graph.nodes[nid].params),
                    }
                )
                graph.nodes.pop(nid)

        # 0.5 wav_out 输入采样率自动跟随源端口（免手填）：
        #     先解析所有输出端口，把源端口采样率注入 wav_out 的 sample_rate 参数，
        #     其输入端口声明为 param:sample_rate，随后按注入值解析，连接校验自然一致。
        resolved_ports: dict[str, ResolvedPort] = {}
        expanded_ports: dict[str, list[dict[str, Any]]] = {}
        for node in graph.nodes.values():
            comp = self.registry.get(node.component)
            if comp is None:
                raise CompileError(f"component not found: {node.component} (node {node.id})")
            port_manifests = _expand_port_manifests(node, comp, task)
            expanded_ports[node.id] = port_manifests
            for port_manifest in port_manifests:
                if port_manifest["direction"] != "output":
                    continue
                key = f"{node.id}:{port_manifest['id']}"
                resolved_ports[key] = _resolve_port_signature(node, comp, port_manifest, task)
        for conn in graph.connections:
            to_node = graph.nodes.get(conn.to_ref.node_id)
            if to_node is None or to_node.component != "orpheus.builtin.wav_out":
                continue
            fp = resolved_ports.get(str(conn.from_ref))
            if fp is not None:
                # 仅在编译内存中注入（compile 不持久化工程），运行时/生成路径经 plan 参数消费
                to_node.params = {**to_node.params, "sample_rate": str(int(fp.sample_rate))}

        # sweep_record：采集参数自动跟随扫频发生器（时长/起始/结束频率/对数）
        # 免手填，且频率轴与发生器一致（参数不一致会提前完结或频率映射错乱）
        sweep_params: dict[str, Any] = {}
        sweep_dur = 0.0
        for node in graph.nodes.values():
            if node.component != "orpheus.builtin.sweep_gen":
                continue
            try:
                sweep_dur = max(sweep_dur, float(node.params.get("duration_s", 0.0) or 0.0))
            except (TypeError, ValueError):
                pass
            for k in ("start_freq", "end_freq", "duration_s", "log_scale"):
                if node.params.get(k) is not None:
                    sweep_params[k] = node.params[k]
        if sweep_params:
            for node in graph.nodes.values():
                if node.component == "orpheus.builtin.sweep_record":
                    node.params = {**node.params, **sweep_params}

        # 1. Resolve remaining ports (inputs; variable-count expansion already done)
        for node in graph.nodes.values():
            comp = self.registry.get(node.component)
            for port_manifest in expanded_ports[node.id]:
                key = f"{node.id}:{port_manifest['id']}"
                if key in resolved_ports:
                    continue
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
        plan.declarations = declarations

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

        # 模块布局：按节点 id 的 `__` 路径前缀组织模块树，DFS 分配稳定模块 id。
        # flatten 只决定执行拓扑；内存布局按模块连续（生成路径=嵌套结构体/动态路径=按模块切片）。
        plan.modules = self._build_module_layout(execution_order)
        # 数据点 ID 表：同一份工程内稳定，动态 Runtime 与代码生成共用同一寻址
        plan.id_map = self._build_id_map(plan, getattr(project, "double_bank", "auto"))

        return plan

    def _module_data_points(self, plan: ExecutionPlan, module: dict) -> list[dict[str, Any]]:
        """模块内数据点：叶子按执行序 × manifest 参数序 + bulk_slots 序（槽序号同此顺序）。"""
        points: list[dict[str, Any]] = []
        for leaf in module.get("leaves", []):
            nid = leaf["node"]
            cfg = plan.node_configs[nid]
            info = self.registry.get(cfg["component"])
            for p in (info.manifest.get("parameters", []) if info else []):
                points.append({**p, "node": nid})
            for bs in (info.manifest.get("bulk_slots", []) if info else []):
                points.append({**bs, "node": nid, "runtime": True})
            for ch in (info.manifest.get("custom_handles", []) if info else []):
                points.append({**ch, "node": nid, "kind": "custom", "type": "string"})
        return points

    def _build_id_map(self, plan: ExecutionPlan, double_bank_mode: str = "auto") -> list[dict[str, Any]]:
        """32 位数据 ID 表（用途 kind + 形式 form + 类型/个数 + 双 bank 生效位），动态/生成两路共用。"""
        entries: list[dict[str, Any]] = []
        for module in plan.modules:
            for slot, p in enumerate(self._module_data_points(plan, module)):
                kind = id_kind_of(p)
                declared = bool(p.get("double_bank", False))
                effective = (
                    True if double_bank_mode == "on"
                    else False if double_bank_mode == "off"
                    else declared
                )
                entries.append(
                    {
                        "id": id_value(kind, module["id"], slot),
                        "node": p["node"],
                        "key": p["id"],
                        "kind": kind,
                        "form": id_form_of(p),
                        "type": p.get("type", "float"),
                        "count": int(p.get("count", 1) or 1),
                        "name": p.get("name", p["id"]),
                        "runtime": bool(p.get("runtime", False)),
                        "double_bank": effective,
                        "reply": bool(p.get("reply", False)),
                    }
                )
        return entries

    def _build_module_layout(self, execution_order: list[str]) -> list[dict[str, Any]]:
        """按 flatId 路径前缀分组：模块=子组件实例路径（根="" 存顶层叶子）。

        - 模块 id：模块树 DFS 前序（根=0，子模块按路径字典序），同一份工程内稳定；
        - 模块内叶子槽：按执行序编号（叶子=该模块的直接子节点）。
        数据点级槽（参数/探针/bulk）由生成器按 manifest 顺序继续展开。
        """
        module_limit = 256  # 8 bit module id
        paths = {""}
        for nid in execution_order:
            parts = nid.split("__")
            for i in range(1, len(parts)):
                paths.add("__".join(parts[:i]))

        def children(path: str) -> list[str]:
            prefix = path + "__" if path else ""
            return sorted(
                q for q in paths
                if q != path and q.startswith(prefix) and "__" not in q[len(prefix):]
            )

        ordered: list[str] = []

        def dfs(path: str) -> None:
            ordered.append(path)
            for child in children(path):
                dfs(child)

        dfs("")
        if len(ordered) > module_limit:
            raise CompileError(
                f"module count {len(ordered)} exceeds {module_limit} (8-bit module id)"
            )
        ids = {path: i for i, path in enumerate(ordered)}

        modules: list[dict[str, Any]] = []
        for path in ordered:
            prefix = path + "__" if path else ""
            leaves = []
            slot = 0
            for nid in execution_order:
                if path == "":
                    direct = "__" not in nid
                else:
                    direct = nid.startswith(prefix) and "__" not in nid[len(prefix):]
                if direct:
                    leaves.append({"node": nid, "slot": slot})
                    slot += 1
            modules.append({"path": path, "id": ids[path], "leaves": leaves})
        return modules

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
