"""Project data model and persistence."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from orpheus_core import schemas


@dataclass
class PortRef:
    """Reference to a node port: 'node_id:port_id'."""
    node_id: str
    port_id: str

    @classmethod
    def parse(cls, s: str) -> PortRef:
        parts = s.split(":")
        if len(parts) != 2:
            raise ValueError(f"invalid port reference: {s}")
        return cls(node_id=parts[0], port_id=parts[1])

    def __str__(self) -> str:
        return f"{self.node_id}:{self.port_id}"


@dataclass
class Node:
    id: str
    component: str
    label: str = ""  # 显示名（可重命名；空=用 id 显示）
    version: str | None = None
    task: str = "default"
    params: dict[str, Any] = field(default_factory=dict)
    position: dict[str, float] = field(default_factory=dict)


@dataclass
class Connection:
    from_ref: PortRef
    to_ref: PortRef


@dataclass
class Graph:
    nodes: dict[str, Node] = field(default_factory=dict)
    connections: list[Connection] = field(default_factory=list)


@dataclass
class SubPort:
    """External port of a subcomponent, mapped to an internal atomic node port."""
    id: str
    direction: str  # "input" | "output"
    maps_to: str    # "inner_node:inner_port", must reference an atomic internal node


@dataclass
class Subcomponent:
    """A project-private composite component wrapping a subgraph."""
    id: str
    name: str = ""
    description: str = ""
    ports: list[SubPort] = field(default_factory=list)
    graph: Graph = field(default_factory=Graph)


@dataclass
class Task:
    id: str
    name: str = ""
    sample_rate: int = 48000
    block_size: int = 128
    priority: int = 0


@dataclass
class Project:
    version: str = "0.1.0"
    metadata: dict[str, Any] = field(default_factory=dict)
    sample_rate: int = 48000
    block_size: int = 128
    buffer_size: int = 0
    double_bank: str = "auto"  # BULK 双 bank：auto=按组件声明 / on=全部 / off=关闭（直写即时生效）
    tasks: dict[str, Task] = field(default_factory=dict)
    graph: Graph = field(default_factory=Graph)
    subcomponents: list[Subcomponent] = field(default_factory=list)
    # 顶层未知字段（如 presets、model_tree 蒸馏注释）：schema 放行但 loader 不认识，
    # 统一收进这里，保证 保存→重载→导出 往返不丢数据。
    extra: dict[str, Any] = field(default_factory=dict)

    @property
    def presets(self) -> list[dict[str, Any]]:
        return self.extra.get("presets", [])

    def get_default_task(self) -> Task:
        if not self.tasks:
            return Task(id="default", name="Default", sample_rate=self.sample_rate, block_size=self.block_size)
        return next(iter(self.tasks.values()))


def _parse_graph(graph_data: dict[str, Any]) -> Graph:
    graph = Graph()
    for n in graph_data.get("nodes", []):
        node = Node(
            id=n["id"],
            component=n["component"],
            label=n.get("label", ""),
            version=n.get("version"),
            task=n.get("task", "default"),
            params=n.get("params", {}),
            position=n.get("position", {}),
        )
        graph.nodes[node.id] = node
    for c in graph_data.get("connections", []):
        graph.connections.append(
            Connection(
                from_ref=PortRef.parse(c["from"]),
                to_ref=PortRef.parse(c["to"]),
            )
        )
    return graph


def _graph_to_dict(graph: Graph) -> dict[str, Any]:
    return {
        "nodes": [
            {
                "id": n.id,
                "component": n.component,
                **({"label": n.label} if n.label else {}),
                **({"version": n.version} if n.version else {}),
                "task": n.task,
                "params": n.params,
                "position": n.position,
            }
            for n in graph.nodes.values()
        ],
        "connections": [
            {"from": str(c.from_ref), "to": str(c.to_ref)}
            for c in graph.connections
        ],
    }


def project_to_dict(project: Project) -> dict[str, Any]:
    """Serialize a Project to the plain dict shape used by YAML/JSON documents."""
    doc = {
        "version": project.version,
        "metadata": project.metadata,
        "sample_rate": project.sample_rate,
        "block_size": project.block_size,
        "buffer_size": project.buffer_size,
        "double_bank": project.double_bank,
        "tasks": [
            {
                "id": t.id,
                "name": t.name,
                "sample_rate": t.sample_rate,
                "block_size": t.block_size,
                "priority": t.priority,
            }
            for t in project.tasks.values()
        ],
        "graph": _graph_to_dict(project.graph),
    }
    if project.subcomponents:
        doc["subcomponents"] = [
            {
                "id": s.id,
                "name": s.name,
                "description": s.description,
                "ports": [
                    {"id": p.id, "direction": p.direction, "maps_to": p.maps_to}
                    for p in s.ports
                ],
                "graph": _graph_to_dict(s.graph),
            }
            for s in project.subcomponents
        ]
    if project.extra:
        doc.update(project.extra)
    return doc


class ProjectLoader:
    def __init__(self) -> None:
        self._schema = schemas.load_project_schema()

    def load(self, path: Path) -> Project:
        with open(path, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
        schemas.validate(data, self._schema)

        project = Project(version=data.get("version", "0.1.0"))
        project.metadata = data.get("metadata", {})
        project.sample_rate = data.get("sample_rate", 48000)
        project.block_size = data.get("block_size", 128)
        project.buffer_size = data.get("buffer_size", 0)
        project.double_bank = data.get("double_bank", "auto")

        for t in data.get("tasks", []):
            task = Task(
                id=t["id"],
                name=t.get("name", t["id"]),
                sample_rate=t.get("sample_rate", project.sample_rate),
                block_size=t.get("block_size", project.block_size),
                priority=t.get("priority", 0),
            )
            project.tasks[task.id] = task
        if not project.tasks:
            project.tasks["default"] = Task(
                id="default",
                name="Default",
                sample_rate=project.sample_rate,
                block_size=project.block_size,
            )

        project.graph = _parse_graph(data.get("graph", {"nodes": [], "connections": []}))
        for s in data.get("subcomponents", []):
            sub = Subcomponent(
                id=s["id"],
                name=s.get("name", s["id"]),
                description=s.get("description", ""),
                ports=[
                    SubPort(id=p["id"], direction=p["direction"], maps_to=p["maps_to"])
                    for p in s.get("ports", [])
                ],
                graph=_parse_graph(s.get("graph", {"nodes": [], "connections": []})),
            )
            project.subcomponents.append(sub)
        # 保留未知顶层字段（presets / model_tree 等），往返不丢
        known = {
            "version", "metadata", "sample_rate", "block_size", "buffer_size",
            "tasks", "graph", "subcomponents",
        }
        for key, value in data.items():
            if key not in known:
                project.extra[key] = value
        return project

    def save(self, project: Project, path: Path) -> None:
        with open(path, "w", encoding="utf-8") as f:
            yaml.safe_dump(project_to_dict(project), f, sort_keys=False, allow_unicode=True)
