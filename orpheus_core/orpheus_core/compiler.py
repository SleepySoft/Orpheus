"""Graph compiler: parse, type-check, derive signatures, schedule."""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any

from orpheus_core.project import Connection, ControlConnection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import ComponentInfo, Registry
from orpheus_core.parameter_catalog import id_form_of, id_kind_of, id_value

# 可作为控制连接目标的 update_policy（restart_required/transactional 不允许运行期被驱动）
_BINDABLE_POLICIES = {"immediate", "smoothed", "block_boundary"}
# control_source 参数必须可读：显式 readback，或 kind 为 probe/state
_CONTROL_SOURCE_KINDS = {"probe", "state"}


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
    block_size_explicit: bool = False


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
    target: str = ""             # 平台解析选定的目标平台（win/dsp/...；空=未解析）
    # 控制链路：编译期校验通过的参数驱动关系（运行时块边界两相快照投递）
    control_links: list[dict[str, Any]] = field(default_factory=list)
    # 静态调度表：主步长 tick（图速率帧）+ 每节点触发周期 period（= 节点间隔 / tick）。
    # 宿主按 tick 推进；runtime/生成路径按 (block_counter+1) % period == 0 触发。
    # 单速率图下 tick==block_size、period==divisor，与旧行为逐字节一致。
    schedule: dict[str, Any] | None = None


def _resolve_atom(expr: Any, node: Node, task: Task) -> Any:
    if isinstance(expr, str) and expr.startswith("param:"):
        param_id = expr[6:]
        return node.params.get(param_id)
    if expr == "task:sample_rate":
        return task.sample_rate
    if expr == "task:block_size":
        return task.block_size
    # in:block_size / in:sample_rate reference this node's input port values;
    # resolved during block-size propagation (initial resolve returns None -> task default).
    if isinstance(expr, str) and expr.startswith("in:"):
        return None
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
    block_size_explicit = port_manifest.get("block_size") is not None
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
        block_size_explicit=block_size_explicit,
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
        resolved_platform = _resolution.platform
        default_task = project.get_default_task()
        resolved_task = self._resolve_source_rate(project, default_task)
        # 时钟源采样率覆盖所有 task，保证跨任务端口解析时 sample_rate 一致
        for t in project.tasks.values():
            t.sample_rate = resolved_task.sample_rate

        def node_task(node: Node) -> Task:
            return project.tasks.get(node.task, resolved_task)

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
            nt = node_task(node)
            port_manifests = _expand_port_manifests(node, comp, nt)
            expanded_ports[node.id] = port_manifests
            for port_manifest in port_manifests:
                if port_manifest["direction"] != "output":
                    continue
                key = f"{node.id}:{port_manifest['id']}"
                resolved_ports[key] = _resolve_port_signature(node, comp, port_manifest, nt)
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
            nt = node_task(node)
            for port_manifest in expanded_ports[node.id]:
                key = f"{node.id}:{port_manifest['id']}"
                if key in resolved_ports:
                    continue
                resolved_ports[key] = _resolve_port_signature(node, comp, port_manifest, nt)

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

        # 2.6 控制连接校验（control_source/bindable/类型/形状），产出 plan.control_links
        control_links = self._validate_control_links(graph, project.control_connections, node_task)

        # 3. Topological sort
        execution_order = self._topological_sort(graph)

        # 3.5 Rate divisor propagation (multi-rate scheduling)
        node_divisor = self._propagate_rate_divisors(graph, expanded_ports, execution_order, resolved_task)

        # 4. Build execution plan
        plan = ExecutionPlan(
            abi_version=1,
            sample_rate=resolved_task.sample_rate,
            block_size=resolved_task.block_size,
            buffer_size=project.buffer_size,
            task_id=resolved_task.id,
            nodes=list(graph.nodes.keys()),
            execution_order=execution_order,
        )
        plan.declarations = declarations
        plan.target = resolved_platform
        plan.control_links = control_links

        # per-node processing quantum: the producer buffer's frame count
        # (differs from task block size in rate-shifted domains)
        def _compute_in_frames(strict: bool = False) -> dict[str, int]:
            frames: dict[str, int] = {}
            for conn in graph.connections:
                to_node = conn.to_ref.node_id
                bs = resolved_ports[str(conn.from_ref)].block_size
                comp = self.registry.get(graph.nodes[to_node].component)
                merging = bool(comp and (comp.manifest.get("scheduling") or {}).get("merge"))
                if merging:
                    # 多速率异步合流：本节点接受多个不同块长输入，向下游暴露公倍数块。
                    prev = frames.get(to_node)
                    frames[to_node] = math.lcm(prev, bs) if prev else bs
                else:
                    if strict and to_node in frames and frames[to_node] != bs:
                        raise CompileError(
                            f"block size mismatch at node {to_node}: inputs differ "
                            f"({frames[to_node]} vs {bs})"
                        )
                    frames[to_node] = bs
            return frames

        in_frames = _compute_in_frames()

        # 未显式声明 block_size 的输出端口继承本节点输入块长，
        # 并在拓扑序上迭代传播，直到链路上所有直通端口块长收敛。
        # 这样 gain/mixer/channel_router/window 等组件无需在每个端口手动声明 block_size。
        for _ in range(len(execution_order) + 1):
            changed = False
            for node_id in execution_order:
                node = graph.nodes[node_id]
                nt = node_task(node)
                node_in_frames = in_frames.get(node_id, nt.block_size)
                for pm in expanded_ports[node_id]:
                    if pm["direction"] != "output":
                        continue
                    key = f"{node_id}:{pm['id']}"
                    rp = resolved_ports[key]
                    bs_expr = pm.get("block_size")
                    if isinstance(bs_expr, str) and "in:block_size" in bs_expr:
                        # rate-change component (e.g. downrate): out block = in block x factor,
                        # auto-discovered from input, not the global task block_size.
                        resolved_bs = _resolve_value(
                            bs_expr.replace("in:block_size", str(node_in_frames)), node, nt)
                        if resolved_bs is not None and int(resolved_bs) != rp.block_size:
                            resolved_ports[key] = replace(rp, block_size=int(resolved_bs))
                            changed = True
                    elif rp.block_size_explicit:
                        continue
                    elif rp.block_size != node_in_frames:
                        resolved_ports[key] = replace(rp, block_size=node_in_frames)
                        changed = True
            if not changed:
                break
            in_frames = _compute_in_frames()

        # Converged: validate that no node merges two different block sizes.
        in_frames = _compute_in_frames(strict=True)

        for node in graph.nodes.values():
            comp = self.registry.get(node.component)
            nt = node_task(node)
            port_manifests = expanded_ports[node.id]
            # effective sample rate: from any resolved port of this node
            node_rate = nt.sample_rate
            for pm in port_manifests:
                rp = resolved_ports.get(f"{node.id}:{pm['id']}")
                if rp is not None:
                    node_rate = rp.sample_rate
                    break
            out_ports = [p for p in port_manifests if p["direction"] == "output"]
            # 每个节点的速率域调度量子（block_size）：源=其 Task 块长，下游=输入超级块长。
            # 显式落盘到 plan，运行/生成两路按其自身值取 config.block_size，
            # 不依赖工程全局 block_size（后者仅是宿主导入默认/旧版回退）。
            node_quantum = in_frames.get(node.id, nt.block_size)
            plan.node_configs[node.id] = {
                "component": node.component,
                "version": comp.version if comp else "",
                "params": dict(node.params),
                "task": node.task,
                "divisor": node_divisor[node.id],
                "block_size": node_quantum,
                "frames": node_quantum,
                "sample_rate": node_rate,
                # ordered port ids: runtime binds buffers by port id, not order
                "input_ports": [p["id"] for p in port_manifests if p["direction"] == "input"],
                "output_ports": [p["id"] for p in out_ports],
                "output_port_block_sizes": {
                    p["id"]: resolved_ports[f"{node.id}:{p['id']}"].block_size
                    for p in out_ports
                },
                "output_port_channels": {
                    p["id"]: resolved_ports[f"{node.id}:{p['id']}"].channels
                    for p in out_ports
                },
            }

        # 4.5 静态调度表：每个节点的触发间隔统一折算为图采样率帧数
        #     I_n = frames_n × 图速率 / 节点流速率（整除不了立即报错，不静默掩盖），
        #     主步长 tick = 所有 I_n 的 GCD，节点周期 period_n = I_n / tick。
        def _node_stream_rate(node_id: str) -> int:
            """节点量子 frames 所在的流速率：有输入取首个输入端口速率，源节点取首个端口。"""
            pms = expanded_ports[node_id]
            inputs = [p for p in pms if p["direction"] == "input"]
            chosen = inputs[0] if inputs else (pms[0] if pms else None)
            if chosen is None:
                return plan.sample_rate
            rp = resolved_ports.get(f"{node_id}:{chosen['id']}")
            return int(rp.sample_rate) if rp is not None else plan.sample_rate

        intervals: dict[str, int] = {}
        for node_id, cfg in plan.node_configs.items():
            frames_n = int(cfg["frames"])
            rate_n = _node_stream_rate(node_id)
            num = frames_n * plan.sample_rate
            if rate_n <= 0 or num % rate_n != 0:
                raise CompileError(
                    f"时钟链不匹配：节点 {node_id} 的触发间隔无法折算为图速率整数帧"
                    f"（frames={frames_n}, node_rate={rate_n}, graph_rate={plan.sample_rate}）"
                )
            intervals[node_id] = num // rate_n
        tick = 0
        for iv in intervals.values():
            tick = iv if tick == 0 else math.gcd(tick, iv)
        if tick <= 0:
            tick = plan.block_size
        periods = {nid: iv // tick for nid, iv in intervals.items()}
        plan.schedule = {"tick": tick, "periods": periods}
        for nid, cfg in plan.node_configs.items():
            cfg["period"] = periods[nid]

        # 离线运行时长：无文件输入时，宿主按时钟源声明的时长跑（信号发生器/扫频发生器/扫频记录
        # 的 duration_s），否则固定 10s 会截断长信号（60s 只跑 10s，或跟着 1s 的 wav 只跑 1s）。
        max_dur = 0.0
        for cfg in plan.node_configs.values():
            if cfg["component"] in ("orpheus.builtin.sweep_gen", "orpheus.builtin.sweep_record",
                                    "orpheus.builtin.signal_gen"):
                try:
                    d = float(cfg["params"].get("duration_s", 0.0) or 0.0)
                except (TypeError, ValueError):
                    d = 0.0
                if d > max_dur:
                    max_dur = d
        if max_dur > 0.0:
            plan.duration_frames = int(max_dur * resolved_task.sample_rate + 0.5)


        # 分配 buffer id：每个连接一个 buffer
        buffer_id = 0
        for conn in graph.connections:
            key = f"buf_{buffer_id}"
            buffer_id += 1
            from_port = resolved_ports[str(conn.from_ref)]
            to_node_id = conn.to_ref.node_id
            to_comp = self.registry.get(graph.nodes[to_node_id].component)
            merging = bool(to_comp and (to_comp.manifest.get("scheduling") or {}).get("merge"))
            frame_count = int(from_port.block_size)
            if merging:
                # rate-bridge：合流（merge）节点的输入边深度 = 合流量子（各源块长的 LCM，
                # 天然是公倍数）。生产者照旧按自己的块长直写 staging，runtime/生成路径
                # 按写游标滚入桥接 buffer，merge 节点在同步点整块读出。
                # 约束：桥接源必须是整写组件（每次触发完整交付输出端口块 = 节点量子）；
                # downrate/resample 等部分产出组件直连接入在此报错，不打表面补丁。
                producer_frames = int(plan.node_configs[conn.from_ref.node_id]["frames"])
                if frame_count != producer_frames:
                    raise CompileError(
                        f"时钟链不匹配：合流节点 {to_node_id} 的源 {conn.from_ref.node_id} "
                        f"不是整写组件（每次触发产出 {producer_frames} 帧，输出端口块长 "
                        f"{frame_count}）；请在分频/重采样之后加一级直通组件（如 gain）再合流"
                    )
                frame_count = int(in_frames[to_node_id])
            plan.buffers[key] = {
                "from": str(conn.from_ref),
                "to": str(conn.to_ref),
                "sample_format": from_port.sample_format,
                "channels": from_port.channels,
                "frame_count": frame_count,
                **({"rate_bridge": True} if merging else {}),
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

    def _resolve_param_shape(
        self, param: dict[str, Any], node: Node, comp: ComponentInfo, task: Task
    ) -> list[int]:
        """求值参数 shape 声明（元素为整数或 ``param:xxx`` 表达式），缺省 = 标量 []。"""
        dims: list[int] = []
        for elem in param.get("shape") or []:
            value = _resolve_value(elem, node, task)
            if value is None and isinstance(elem, str) and elem.startswith("param:"):
                # 节点未显式给值时回退到 manifest 默认值（与端口 count 展开同一策略）
                param_id = elem[6:]
                for p in comp.manifest.get("parameters", []):
                    if p["id"] == param_id:
                        value = p.get("default")
                        break
            try:
                value = int(value)
            except (TypeError, ValueError) as exc:
                raise CompileError(
                    f"无法求值参数形状：节点 {node.id} 参数 {param['id']} 的 shape 元素 {elem!r}"
                ) from exc
            if value < 1:
                raise CompileError(
                    f"非法参数形状：节点 {node.id} 参数 {param['id']} 的维度 {value} < 1"
                )
            dims.append(value)
        return dims

    def _validate_control_links(
        self,
        graph: Graph,
        control_connections: list[ControlConnection],
        node_task,
    ) -> list[dict[str, Any]]:
        """校验控制连接并产出 plan.control_links。

        规则（design_control_link_eval §3/§8）：源参数须声明 control_source 且可读；
        目标参数须声明 bindable、非 affects_signature、update_policy 合规；
        两端类型严格相同、shape 按各自节点参数求值后严格相等（禁止隐式转换）。
        """
        links: list[dict[str, Any]] = []
        driven_targets: set[str] = set()  # 同一目标参数只允许一个控制源（两相快照下同块两写是模糊行为）
        for cc in control_connections:
            src_ref, dst_ref = cc.from_ref, cc.to_ref
            if str(dst_ref) in driven_targets:
                raise CompileError(
                    f"控制连接目标重复：{dst_ref} 已被其他控制连接驱动；"
                    f"同一参数只能有一个控制源（需汇聚时请显式加适配组件）"
                )
            driven_targets.add(str(dst_ref))
            src_node = graph.nodes.get(src_ref.node_id)
            if src_node is None:
                raise CompileError(f"控制连接源节点不存在：{src_ref.node_id}（{src_ref}）")
            dst_node = graph.nodes.get(dst_ref.node_id)
            if dst_node is None:
                raise CompileError(f"控制连接目标节点不存在：{dst_ref.node_id}（{dst_ref}）")
            src_comp = self.registry.get(src_node.component)
            dst_comp = self.registry.get(dst_node.component)

            def param_of(comp: ComponentInfo, param_id: str) -> dict[str, Any] | None:
                for p in comp.manifest.get("parameters", []):
                    if p["id"] == param_id:
                        return p
                return None

            src_param = param_of(src_comp, src_ref.port_id) if src_comp else None
            if src_param is None:
                raise CompileError(f"控制连接源参数不存在：{src_ref}（组件 {src_node.component}）")
            if not src_param.get("control_source"):
                raise CompileError(
                    f"控制连接源参数未声明 control_source：{src_ref}；"
                    f"请在组件 manifest 中为该参数加 control_source: true"
                )
            if not (src_param.get("readback") or src_param.get("kind") in _CONTROL_SOURCE_KINDS):
                raise CompileError(
                    f"控制连接源参数不可读：{src_ref}；control_source 要求 readback: true 或 kind 为 probe/state"
                )
            dst_param = param_of(dst_comp, dst_ref.port_id) if dst_comp else None
            if dst_param is None:
                raise CompileError(f"控制连接目标参数不存在：{dst_ref}（组件 {dst_node.component}）")
            if not dst_param.get("bindable"):
                raise CompileError(
                    f"控制连接目标参数不允许绑定：{dst_ref}；"
                    f"请在组件 manifest 中为该参数加 bindable: true"
                )
            if dst_param.get("affects_signature"):
                raise CompileError(
                    f"控制连接目标参数影响端口签名（affects_signature），不允许绑定：{dst_ref}"
                )
            policy = dst_param.get("update_policy", "immediate")
            if policy not in _BINDABLE_POLICIES:
                raise CompileError(
                    f"控制连接目标参数更新策略不允许绑定：{dst_ref}（update_policy: {policy}）；"
                    f"仅支持 immediate/smoothed/block_boundary"
                )
            src_type = src_param.get("type", "float")
            dst_type = dst_param.get("type", "float")
            if src_type != dst_type:
                raise CompileError(
                    f"控制连接类型不匹配：{src_ref}（{src_type}）→ {dst_ref}（{dst_type}）；禁止隐式转换"
                )
            src_shape = self._resolve_param_shape(src_param, src_node, src_comp, node_task(src_node))
            dst_shape = self._resolve_param_shape(dst_param, dst_node, dst_comp, node_task(dst_node))
            if src_shape != dst_shape:
                fmt = lambda s: "标量" if not s else "[" + "×".join(str(d) for d in s) + "]"
                raise CompileError(
                    f"控制连接形状不匹配：{src_ref}（{fmt(src_shape)}）→ {dst_ref}（{fmt(dst_shape)}）"
                )
            count = 1
            for d in dst_shape:
                count *= d
            links.append(
                {
                    "src_node": src_ref.node_id,
                    "src_param": src_ref.port_id,
                    "dst_node": dst_ref.node_id,
                    "dst_param": dst_ref.port_id,
                    "type": dst_type,
                    "shape": dst_shape,
                    "count": count,
                }
            )
        return links

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
            comp = self.registry.get(node.component)
            merging = bool(comp and (comp.manifest.get("scheduling") or {}).get("merge"))
            if len(up) > 1 and not merging:
                raise CompileError(
                    f"rate mismatch at node {node_id}: input divisors {sorted(up)} "
                    f"(merge different rates via appropriate rate components)"
                )
            # merge 节点输入可来自不同速率域：向下游暴露其中最慢的调度除数
            # （与块长 LCM 语义一致：输出按公倍数 tick 对齐）。
            d_in = max(up) if up else 1
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
