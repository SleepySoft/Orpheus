"""显式驱动图：调用关系独立于数据绑定，节拍只读取声明。"""

from __future__ import annotations

from dataclasses import dataclass, replace
from fractions import Fraction

from orpheus_core.compiler import CompileError, _resolve_value
from orpheus_core.project import Project, Task
from orpheus_core.registry import Registry


@dataclass
class TaskFlow:
    order: list[str]
    roots: dict[str, str]
    ancestors: dict[str, set[str]]
    cadences: dict[str, tuple[int, int]]
    first_calls: dict[str, int]

    def require_before(self, source: str, target: str) -> None:
        if self.first_calls[source] >= self.first_calls[target]:
            raise CompileError(
                f"Task 顺序未声明：{source} 必须在 {target} 之前调用；"
                "请连接显式驱动路径，数据线/参数线不会补充调用顺序"
            )


def build_task_flow(project: Project, registry: Registry) -> TaskFlow:
    nodes = {
        nid: node for nid, node in project.graph.nodes.items()
        if not (registry.get(node.component) and
                registry.get(node.component).manifest.get("execution", {}).get("none"))
    }
    incoming = {nid: [] for nid in nodes}
    outgoing = {nid: [] for nid in nodes}
    seen: set[tuple[str, str]] = set()
    for edge in project.drive_connections:
        src, dst = edge.from_ref.node_id, edge.to_ref.node_id
        if src not in nodes or dst not in nodes:
            raise CompileError(f"驱动连接引用不存在或不可执行的节点：{edge.from_ref} → {edge.to_ref}")
        if edge.from_ref.port_id != "out" or edge.to_ref.port_id != "in":
            raise CompileError("驱动接口必须使用 node:out → node:in（与数据端口分别命名）")
        if (src, dst) in seen:
            raise CompileError(f"驱动连接重复：{src} → {dst}")
        seen.add((src, dst))
        if (type(edge.every) is not int or edge.every < 1 or
                type(edge.phase) is not int or not 0 <= edge.phase < edge.every):
            raise CompileError(f"驱动连接 {src} → {dst}：every 必须为正整数，phase 必须在 [0, every) 内")
        incoming[dst].append(edge)
        outgoing[src].append(edge)

    degree = {nid: len(edges) for nid, edges in incoming.items()}
    queue = [nid for nid, count in degree.items() if not count]
    order: list[str] = []
    roots: dict[str, str] = {}
    ancestors: dict[str, set[str]] = {}
    cadences: dict[str, tuple[int, int]] = {}
    tasks: dict[str, Task] = {}
    template = project.get_default_task()
    for nid in queue:
        info = registry.get(nodes[nid].component)
        if info is None or not info.manifest.get("clock_source"):
            raise CompileError(f"节点 {nid} 没有驱动来源；Task 必须从 clock_source 源开始")
        roots[nid] = nid
        ancestors[nid] = set()
        cadences[nid] = (1, 0)
        old_task = project.tasks.get(nodes[nid].task, template)
        if nodes[nid].task != "default" and nodes[nid].task not in project.tasks:
            raise CompileError(f"Source {nid} 引用不存在的 Task：{nodes[nid].task}")
        task_id = nodes[nid].task if nodes[nid].task != "default" else f"task_{nid}"
        if task_id in tasks:
            raise CompileError(f"每个 Source 必须独占逻辑 Task：{task_id} 包含多个源")
        task = replace(old_task, id=task_id, name=nodes[nid].label or nid)
        if nodes[nid].component == "orpheus.builtin.task_source":
            hz = nodes[nid].params.get("frequency_hz", 100)
            if type(hz) is not int or hz < 1 or hz > 192000 or task.sample_rate % hz:
                raise CompileError(
                    f"任务源 {nid}：frequency_hz 必须为正整数且整除 Task 时间基准 "
                    f"{task.sample_rate} Hz；不能用取整改变声明的调用频率"
                )
            task = replace(task, block_size=task.sample_rate // hz)
        tasks[task_id] = task
        nodes[nid].task = task_id

    index = 0
    while index < len(queue):
        nid = queue[index]
        index += 1
        order.append(nid)
        for edge in outgoing[nid]:
            dst = edge.to_ref.node_id
            degree[dst] -= 1
            if degree[dst]:
                continue
            info = registry.get(nodes[dst].component)
            if info and info.manifest.get("clock_source"):
                raise CompileError(f"Source {dst} 不能被另一个 Source 调用；请使用独立 Task 和显式跨 Task 桥")
            parents = incoming[dst]
            source_roots = {roots[e.from_ref.node_id] for e in parents}
            if len(source_roots) != 1:
                raise CompileError(f"节点 {dst} 被多个 Source/Task 直接调用；请通过显式跨 Task 桥交付数据")
            rates = []
            ancestors[dst] = set()
            for e in parents:
                src = e.from_ref.node_id
                period, phase = cadences[src]
                rates.append((period * e.every, phase + period * e.phase))
                ancestors[dst].update(ancestors[src] | {src})
            roots[dst] = next(iter(source_roots))
            nodes[dst].task = nodes[roots[dst]].task
            cadences[dst] = rates[0]
            queue.append(dst)
    if len(order) != len(nodes):
        raise CompileError("驱动图包含循环；当前 Task 顺序模式要求显式无环调用，不将环猜成循环语句")
    if not order:
        raise CompileError("Task 顺序模式至少需要一个 Source")
    first_calls: dict[str, int] = {}
    stack = list(reversed([nid for nid in order if roots[nid] == nid]))
    while stack:
        nid = stack.pop()
        if nid in first_calls:
            continue
        first_calls[nid] = len(first_calls)
        stack.extend(e.to_ref.node_id for e in reversed(outgoing[nid]))
    project.tasks = tasks
    return TaskFlow(order, roots, ancestors, cadences, first_calls)


def output_cadence(manifest: dict, port: dict, node, task: Task) -> tuple[int, int]:
    contract = port.get("cadence", {})
    scheduling = manifest.get("scheduling") or {}
    period = _resolve_value(contract.get("period", scheduling.get("divisor", 1)),
                            node, task, exact=True)
    phase = _resolve_value(contract.get("phase", period - 1 if period is not None else -1),
                           node, task, exact=True)
    if (period is None or phase is None or Fraction(period).denominator != 1 or
            Fraction(phase).denominator != 1 or period < 1 or not 0 <= phase < period):
        raise CompileError(f"节点 {node.id} 端口 {port['id']} 的交付周期/相位必须为合法整数")
    return int(period), int(phase)
