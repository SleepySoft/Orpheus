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
    tasks: dict[str, Task] = field(default_factory=dict)
    graph: Graph = field(default_factory=Graph)

    def get_default_task(self) -> Task:
        if not self.tasks:
            return Task(id="default", name="Default", sample_rate=self.sample_rate, block_size=self.block_size)
        return next(iter(self.tasks.values()))


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

        graph_data = data.get("graph", {"nodes": [], "connections": []})
        for n in graph_data.get("nodes", []):
            node = Node(
                id=n["id"],
                component=n["component"],
                version=n.get("version"),
                task=n.get("task", "default"),
                params=n.get("params", {}),
                position=n.get("position", {}),
            )
            project.graph.nodes[node.id] = node
        for c in graph_data.get("connections", []):
            project.graph.connections.append(
                Connection(
                    from_ref=PortRef.parse(c["from"]),
                    to_ref=PortRef.parse(c["to"]),
                )
            )
        return project

    def save(self, project: Project, path: Path) -> None:
        data = {
            "version": project.version,
            "metadata": project.metadata,
            "sample_rate": project.sample_rate,
            "block_size": project.block_size,
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
            "graph": {
                "nodes": [
                    {
                        "id": n.id,
                        "component": n.component,
                        **({"version": n.version} if n.version else {}),
                        "task": n.task,
                        "params": n.params,
                        "position": n.position,
                    }
                    for n in project.graph.nodes.values()
                ],
                "connections": [
                    {"from": str(c.from_ref), "to": str(c.to_ref)}
                    for c in project.graph.connections
                ],
            },
        }
        with open(path, "w", encoding="utf-8") as f:
            yaml.safe_dump(data, f, sort_keys=False, allow_unicode=True)
