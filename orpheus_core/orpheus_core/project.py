"""Project data model and persistence."""

from __future__ import annotations

from dataclasses import dataclass, field
import copy
import hashlib
from pathlib import Path
import json
import math
import struct
from typing import Any

import yaml

from orpheus_core import schemas
from orpheus_core.registry import ComponentInfo, Registry


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
    alters: list[str] = field(default_factory=list)  # 用户声明的替代组（同图节点 id）
    param_resources: dict[str, dict[str, Any]] = field(default_factory=dict, repr=False)


@dataclass
class Connection:
    from_ref: PortRef
    to_ref: PortRef


@dataclass
class Graph:
    nodes: dict[str, Node] = field(default_factory=dict)
    connections: list[Connection] = field(default_factory=list)


@dataclass
class ControlConnection:
    """控制连接：源节点 control_source 参数 → 目标节点 bindable 参数。

    from/to 均为 ``node:param`` 引用（复用 PortRef 的解析，port_id 即参数 id）。
    """
    from_ref: PortRef
    to_ref: PortRef


@dataclass
class SubPort:
    """External port of a subcomponent, mapped to an internal atomic node port."""
    id: str
    direction: str  # "input" | "output"
    maps_to: str    # "inner_node:inner_port", must reference an atomic internal node


@dataclass
class SubParameter:
    """Public control parameter mapped to an internal atomic node parameter."""
    id: str
    direction: str  # "input" (bindable) | "output" (control source)
    maps_to: str
    name: str = ""
    type: str = "float"
    default: Any = None
    shape: list[Any] = field(default_factory=list)
    update_policy: str = "immediate"


@dataclass
class Subcomponent:
    """A project-private composite component wrapping a subgraph."""
    id: str
    name: str = ""
    description: str = ""
    ports: list[SubPort] = field(default_factory=list)
    public_parameters: list[SubParameter] = field(default_factory=list)
    graph: Graph = field(default_factory=Graph)
    imported: bool = False
    source_path: Path | None = None
    read_only: bool = False


@dataclass
class Task:
    id: str
    name: str = ""
    sample_rate: int = 48000
    block_size: int = 128
    priority: int = 0
    clock_domain: str = ""
    trigger_group: str = ""


@dataclass
class ClockDomain:
    id: str
    sample_rate: int
    assurance: str = "derived"


@dataclass
class TriggerGroup:
    id: str
    clock_domain: str
    dispatch: str = "caller"
    master: str = ""
    members: list[str] = field(default_factory=list)


@dataclass
class SportStream:
    resource: str
    slots: list[int]
    channels: list[int]
    slot_count: int
    format: str = "q1_31"


@dataclass
class SportBinding:
    node: str
    streams: list[SportStream] = field(default_factory=list)


@dataclass
class Bridge:
    """声明式外部访问桥；不参与音频执行图。"""
    id: str
    transport: str
    codec: str = "olink"
    enabled: bool = True
    params: dict[str, Any] = field(default_factory=dict)
    position: dict[str, float] = field(default_factory=dict)


@dataclass
class Project:
    version: str = "0.1.0"
    metadata: dict[str, Any] = field(default_factory=dict)
    sample_rate: int = 48000
    block_size: int = 128
    buffer_size: int = 0
    double_bank: str = "auto"  # BULK 双 bank：auto=按组件声明 / on=全部 / off=关闭（直写即时生效）
    target: str = "auto"  # 期望目标平台：auto / win / dsp（解析与警告用，缺省自动）
    debug_mode: bool = False  # 调试旁路：忽略孤立节点和未接入有效时钟源的残留流
    tasks: dict[str, Task] = field(default_factory=dict)
    clock_domains: dict[str, ClockDomain] = field(default_factory=dict)
    trigger_groups: dict[str, TriggerGroup] = field(default_factory=dict)
    sport_bindings: list[SportBinding] = field(default_factory=list)
    bridges: list[Bridge] = field(default_factory=list)
    graph: Graph = field(default_factory=Graph)
    subcomponents: list[Subcomponent] = field(default_factory=list)
    imports: list[Any] = field(default_factory=list)
    root_dir: Path | None = field(default=None, repr=False, compare=False)
    component_manifests: list[Path] = field(default_factory=list, repr=False, compare=False)
    registry: Registry | None = field(default=None, repr=False, compare=False)
    resources: dict[str, dict[str, Any]] = field(default_factory=dict)
    # 控制连接（顶层段）：编译期校验后进入 plan.control_links，运行期块边界两相快照投递
    control_connections: list[ControlConnection] = field(default_factory=list)
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


def _parse_graph(
    graph_data: dict[str, Any], resource_values: dict[str, list[float]] | None = None
) -> Graph:
    graph = Graph()
    for n in graph_data.get("nodes", []):
        params = dict(n.get("params", {}) or {})
        param_resources: dict[str, dict[str, Any]] = {}
        for param_id, value in list(params.items()):
            if not isinstance(value, dict) or "$resource" not in value:
                continue
            resource_id = value["$resource"]
            if not resource_values or resource_id not in resource_values:
                raise ValueError(f"node {n['id']}: undefined resource {resource_id!r}")
            numbers = resource_values[resource_id]
            offset = int(value.get("offset", 0))
            count = int(value.get("count", len(numbers) - offset))
            if offset < 0 or count < 0 or offset + count > len(numbers):
                raise ValueError(
                    f"node {n['id']}: resource {resource_id!r} slice "
                    f"[{offset}:{offset + count}] exceeds {len(numbers)} values"
                )
            params[param_id] = ",".join(
                format(number, ".9g") for number in numbers[offset:offset + count]
            )
            param_resources[param_id] = copy.deepcopy(value)
        node = Node(
            id=n["id"],
            component=n["component"],
            label=n.get("label", ""),
            version=n.get("version"),
            task=n.get("task", "default"),
            params=params,
            position=n.get("position", {}),
            alters=list(n.get("alters", []) or []),
            param_resources=param_resources,
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
                "params": {
                    key: (copy.deepcopy(n.param_resources[key]) if key in n.param_resources else value)
                    for key, value in n.params.items()
                },
                "position": n.position,
                **({"alters": n.alters} if n.alters else {}),
            }
            for n in graph.nodes.values()
        ],
        "connections": [
            {"from": str(c.from_ref), "to": str(c.to_ref)}
            for c in graph.connections
        ],
    }


def project_to_dict(
    project: Project, *, materialize_imports: bool = False
) -> dict[str, Any]:
    """Serialize a Project to the plain dict shape used by YAML/JSON documents."""
    doc = {
        "version": project.version,
        "metadata": project.metadata,
        "sample_rate": project.sample_rate,
        "block_size": project.block_size,
        "buffer_size": project.buffer_size,
        "double_bank": project.double_bank,
        "target": project.target,
        "debug_mode": project.debug_mode,
        "tasks": [
            {
                "id": t.id,
                "name": t.name,
                "sample_rate": t.sample_rate,
                "block_size": t.block_size,
                "priority": t.priority,
                **({"clock_domain": t.clock_domain} if t.clock_domain else {}),
                **({"trigger_group": t.trigger_group} if t.trigger_group else {}),
            }
            for t in project.tasks.values()
        ],
        "graph": _graph_to_dict(project.graph),
    }
    if project.imports:
        doc["imports"] = project.imports
    if project.resources:
        doc["resources"] = project.resources
    if project.clock_domains:
        doc["clock_domains"] = [
            {
                "id": domain.id,
                "sample_rate": domain.sample_rate,
                "assurance": domain.assurance,
            }
            for domain in project.clock_domains.values()
        ]
    if project.trigger_groups:
        doc["trigger_groups"] = [
            {
                "id": trigger.id,
                "clock_domain": trigger.clock_domain,
                "dispatch": trigger.dispatch,
                **({"master": trigger.master} if trigger.master else {}),
                "members": trigger.members,
            }
            for trigger in project.trigger_groups.values()
        ]
    if project.sport_bindings:
        doc["sport_bindings"] = [
            {
                "node": binding.node,
                "streams": [
                    {
                        "resource": stream.resource,
                        "slots": stream.slots,
                        "channels": stream.channels,
                        "slot_count": stream.slot_count,
                        "format": stream.format,
                    }
                    for stream in binding.streams
                ],
            }
            for binding in project.sport_bindings
        ]
    if project.bridges:
        doc["bridges"] = [
            {
                "id": bridge.id,
                "transport": bridge.transport,
                "codec": bridge.codec,
                "enabled": bridge.enabled,
                **({"params": bridge.params} if bridge.params else {}),
                **({"position": bridge.position} if bridge.position else {}),
            }
            for bridge in project.bridges
        ]
    if project.control_connections:
        doc["control_connections"] = [
            {"from": str(c.from_ref), "to": str(c.to_ref)}
            for c in project.control_connections
        ]
    persisted_subcomponents = [
        sub for sub in project.subcomponents if materialize_imports or not sub.imported
    ]
    if persisted_subcomponents:
        doc["subcomponents"] = [
            {
                "id": s.id,
                "name": s.name,
                "description": s.description,
                **({"read_only": True} if s.read_only else {}),
                "ports": [
                    {"id": p.id, "direction": p.direction, "maps_to": p.maps_to}
                    for p in s.ports
                ],
                **({
                    "public_parameters": [
                        {
                            "id": p.id,
                            "direction": p.direction,
                            "maps_to": p.maps_to,
                            **({"name": p.name} if p.name else {}),
                            "type": p.type,
                            **({"default": p.default} if p.default is not None else {}),
                            **({"shape": p.shape} if p.shape else {}),
                            "update_policy": p.update_policy,
                        }
                        for p in s.public_parameters
                    ]
                } if s.public_parameters else {}),
                "graph": _graph_to_dict(s.graph),
            }
            for s in persisted_subcomponents
        ]
    if project.extra:
        doc.update(project.extra)
    return doc


def _flatten_numbers(value: Any) -> list[float]:
    if isinstance(value, list):
        output: list[float] = []
        for item in value:
            output.extend(_flatten_numbers(item))
        return output
    return [float(value)]


def _load_resource_values(
    definitions: dict[str, dict[str, Any]], base: Path
) -> dict[str, list[float]]:
    values: dict[str, list[float]] = {}
    resolved_base = base.resolve()
    for resource_id, definition in definitions.items():
        relative = definition.get("file")
        if not isinstance(relative, str) or not relative:
            raise ValueError(f"resource {resource_id!r} missing file")
        path = (resolved_base / relative).resolve()
        try:
            path.relative_to(resolved_base)
        except ValueError as exc:
            raise ValueError(f"resource {resource_id!r} escapes its project/package") from exc
        raw = path.read_bytes()
        expected_hash = definition.get("sha256")
        digest = hashlib.sha256(raw).hexdigest()
        if expected_hash and digest != expected_hash:
            raise ValueError(
                f"resource {resource_id!r} sha256 mismatch: expected {expected_hash}, got {digest}"
            )
        resource_format = definition.get("format", "json")
        if resource_format == "f32le":
            if len(raw) % 4:
                raise ValueError(f"resource {resource_id!r} f32le byte length is not divisible by 4")
            numbers = list(struct.unpack(f"<{len(raw) // 4}f", raw))
        elif resource_format == "json":
            decoded = json.loads(raw.decode("utf-8"))
            if isinstance(decoded, dict):
                decoded = decoded.get("values", decoded.get("data"))
            numbers = _flatten_numbers(decoded)
        elif resource_format == "csv":
            text = raw.decode("utf-8").replace("\n", ",")
            numbers = [float(token.strip()) for token in text.split(",") if token.strip()]
        else:
            raise ValueError(f"resource {resource_id!r} has unsupported format {resource_format!r}")
        shape = definition.get("shape", []) or []
        if shape:
            expected_count = math.prod(int(dimension) for dimension in shape)
            if len(numbers) != expected_count:
                raise ValueError(
                    f"resource {resource_id!r} shape {shape} requires {expected_count} values, "
                    f"got {len(numbers)}"
                )
        values[resource_id] = numbers
    return values


def _subcomponent_from_manifest(info: ComponentInfo) -> Subcomponent:
    manifest = info.manifest
    if info.package_type != "composite":
        raise ValueError(f"component {info.id!r} is not composite")
    ports = []
    for port in manifest.get("ports", []) or []:
        if not port.get("maps_to"):
            raise ValueError(f"composite {info.id}: port {port.get('id')!r} missing maps_to")
        ports.append(SubPort(
            id=port["id"], direction=port["direction"], maps_to=port["maps_to"]
        ))
    public_parameters = []
    for parameter in manifest.get("parameters", []) or []:
        if not parameter.get("maps_to"):
            continue
        public_parameters.append(SubParameter(
            id=parameter["id"],
            direction=parameter.get("direction", "input"),
            maps_to=parameter["maps_to"],
            name=parameter.get("name", parameter["id"]),
            type=parameter.get("type", "float"),
            default=parameter.get("default"),
            shape=list(parameter.get("shape", []) or []),
            update_policy=parameter.get("update_policy", "immediate"),
        ))
    resource_values = _load_resource_values(
        dict(manifest.get("resources", {}) or {}), info.root_dir
    )
    return Subcomponent(
        id=info.id,
        name=manifest.get("name", info.id),
        description=manifest.get("description", ""),
        ports=ports,
        public_parameters=public_parameters,
        graph=_parse_graph(
            manifest.get("graph", {"nodes": [], "connections": []}),
            resource_values,
        ),
        imported=True,
        source_path=info.manifest_path,
    )


class ProjectLoader:
    def __init__(self, registry: Registry | None = None) -> None:
        self._schema = schemas.load_project_schema()
        self._registry = registry

    @staticmethod
    def _entry_path(path: Path) -> Path:
        candidate = Path(path)
        return candidate / "project.yaml" if candidate.is_dir() else candidate

    @staticmethod
    def _secure_path(base: Path, owner: Path, relative: str) -> Path:
        requested = Path(relative)
        if requested.is_absolute():
            raise ValueError(f"component import must be relative: {relative}")
        target = (owner.parent / requested).resolve()
        try:
            target.relative_to(base)
        except ValueError as exc:
            raise ValueError(f"component import escapes project directory: {relative}") from exc
        return target

    def _load_imports(self, project: Project, entry_path: Path, registry: Registry) -> None:
        if not project.imports:
            return
        base = entry_path.parent.resolve()
        seen_paths: set[Path] = set()
        seen_ids: dict[str, Path] = {}
        resolving_ids: list[str] = []

        def register_info(info: ComponentInfo) -> None:
            previous = seen_ids.get(info.id)
            if previous is not None and previous != info.manifest_path:
                raise ValueError(
                    f"duplicate imported component id {info.id!r}: {previous} and {info.manifest_path}"
                )
            seen_ids[info.id] = info.manifest_path
            if info.manifest_path not in project.component_manifests:
                project.component_manifests.append(info.manifest_path)
            if info.package_type == "composite" and not any(
                sub.id == info.id for sub in project.subcomponents
            ):
                subcomponent = _subcomponent_from_manifest(info)
                try:
                    info.manifest_path.resolve().relative_to(base)
                    subcomponent.read_only = False
                except ValueError:
                    subcomponent.read_only = True
                project.subcomponents.append(subcomponent)

        def visit_file(manifest_path: Path, containment: Path) -> None:
            resolved = manifest_path.resolve()
            if resolved in seen_paths:
                return
            seen_paths.add(resolved)
            if not resolved.is_file():
                raise FileNotFoundError(f"component import not found: {resolved}")
            info = registry.add_manifest(resolved)
            register_info(info)
            for nested in info.manifest.get("imports", []) or []:
                visit_import(nested, resolved, containment)

        def visit_import(item: Any, owner: Path, containment: Path) -> None:
            if isinstance(item, dict):
                component_id = item.get("component")
                if not component_id:
                    raise ValueError(f"invalid component import: {item!r}")
                info = registry.get(component_id)
                if info is None:
                    raise ValueError(f"global component import not found: {component_id}")
                if component_id in resolving_ids:
                    cycle = " -> ".join([*resolving_ids, component_id])
                    raise ValueError(f"component import cycle: {cycle}")
                required_version = item.get("version")
                if required_version and info.version != required_version:
                    raise ValueError(
                        f"global component {component_id!r} version mismatch: "
                        f"required {required_version}, found {info.version}"
                    )
                required_hash = item.get("sha256")
                if required_hash:
                    actual_hash = hashlib.sha256(info.manifest_path.read_bytes()).hexdigest()
                    if actual_hash != required_hash:
                        raise ValueError(
                            f"global component {component_id!r} manifest sha256 mismatch"
                        )
                register_info(info)
                resolving_ids.append(component_id)
                try:
                    for nested in info.manifest.get("imports", []) or []:
                        visit_import(nested, info.manifest_path, info.root_dir.resolve())
                finally:
                    resolving_ids.pop()
                return
            if not isinstance(item, str) or not item:
                raise ValueError(f"invalid component import: {item!r}")
            target = self._secure_path(containment, owner, item)
            if target.is_dir():
                manifests = set(target.rglob("component.yaml"))
                manifests.update(target.rglob("*.component.yaml"))
                if not manifests:
                    raise FileNotFoundError(f"component import directory is empty: {target}")
                for path in sorted(manifests):
                    visit_file(path, containment)
            else:
                visit_file(target, containment)

        for imported in project.imports:
            visit_import(imported, entry_path, base)

    def load(self, path: Path) -> Project:
        path = self._entry_path(path).resolve()
        with open(path, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
        schemas.validate(data, self._schema)

        project = Project(version=data.get("version", "0.1.0"))
        project.registry = self._registry.fork() if self._registry is not None else Registry()
        project.root_dir = path.parent
        project.imports = list(data.get("imports", []) or [])
        project.resources = dict(data.get("resources", {}) or {})
        resource_values = _load_resource_values(project.resources, path.parent)
        project.metadata = data.get("metadata", {})
        project.sample_rate = data.get("sample_rate", 48000)
        project.block_size = data.get("block_size", 128)
        project.buffer_size = data.get("buffer_size", 0)
        project.double_bank = data.get("double_bank", "auto")
        project.target = data.get("target", "auto")
        project.debug_mode = bool(data.get("debug_mode", False))

        for t in data.get("tasks", []):
            task = Task(
                id=t["id"],
                name=t.get("name", t["id"]),
                sample_rate=t.get("sample_rate", project.sample_rate),
                block_size=t.get("block_size", project.block_size),
                priority=t.get("priority", 0),
                clock_domain=t.get("clock_domain", ""),
                trigger_group=t.get("trigger_group", ""),
            )
            project.tasks[task.id] = task

        project.clock_domains = {
            domain["id"]: ClockDomain(
                id=domain["id"],
                sample_rate=domain["sample_rate"],
                assurance=domain.get("assurance", "derived"),
            )
            for domain in data.get("clock_domains", []) or []
        }
        project.trigger_groups = {
            trigger["id"]: TriggerGroup(
                id=trigger["id"],
                clock_domain=trigger["clock_domain"],
                dispatch=trigger.get("dispatch", "caller"),
                master=trigger.get("master", ""),
                members=list(trigger.get("members", []) or []),
            )
            for trigger in data.get("trigger_groups", []) or []
        }
        project.sport_bindings = [
            SportBinding(
                node=binding["node"],
                streams=[
                    SportStream(
                        resource=stream["resource"],
                        slots=list(stream["slots"]),
                        channels=list(stream["channels"]),
                        slot_count=stream["slot_count"],
                        format=stream.get("format", "q1_31"),
                    )
                    for stream in binding.get("streams", [])
                ],
            )
            for binding in data.get("sport_bindings", []) or []
        ]
        if not project.tasks:
            project.tasks["default"] = Task(
                id="default",
                name="Default",
                sample_rate=project.sample_rate,
                block_size=project.block_size,
            )

        project.bridges = [
            Bridge(
                id=bridge["id"],
                transport=bridge["transport"],
                codec=bridge.get("codec", "olink"),
                enabled=bool(bridge.get("enabled", True)),
                params=dict(bridge.get("params", {}) or {}),
                position=dict(bridge.get("position", {}) or {}),
            )
            for bridge in data.get("bridges", []) or []
        ]

        project.graph = _parse_graph(
            data.get("graph", {"nodes": [], "connections": []}), resource_values
        )
        for c in data.get("control_connections", []) or []:
            project.control_connections.append(
                ControlConnection(
                    from_ref=PortRef.parse(c["from"]),
                    to_ref=PortRef.parse(c["to"]),
                )
            )
        for s in data.get("subcomponents", []):
            sub = Subcomponent(
                id=s["id"],
                name=s.get("name", s["id"]),
                description=s.get("description", ""),
                ports=[
                    SubPort(id=p["id"], direction=p["direction"], maps_to=p["maps_to"])
                    for p in s.get("ports", [])
                ],
                public_parameters=[
                    SubParameter(
                        id=p["id"], direction=p["direction"], maps_to=p["maps_to"],
                        name=p.get("name", p["id"]), type=p.get("type", "float"),
                        default=p.get("default"), shape=list(p.get("shape", []) or []),
                        update_policy=p.get("update_policy", "immediate"),
                    )
                    for p in s.get("public_parameters", [])
                ],
                graph=_parse_graph(
                    s.get("graph", {"nodes": [], "connections": []}), resource_values
                ),
                read_only=bool(s.get("read_only", False)),
            )
            project.subcomponents.append(sub)
        self._load_imports(project, path, project.registry)
        # 保留未知顶层字段（presets / model_tree 等），往返不丢
        known = {
            "version", "metadata", "sample_rate", "block_size", "buffer_size",
            "double_bank", "target", "debug_mode", "tasks", "clock_domains", "trigger_groups",
            "sport_bindings", "bridges", "graph", "subcomponents",
            "control_connections", "imports", "resources",
        }
        for key, value in data.items():
            if key not in known:
                project.extra[key] = value
        return project

    def save(self, project: Project, path: Path) -> None:
        with open(path, "w", encoding="utf-8") as f:
            yaml.safe_dump(project_to_dict(project), f, sort_keys=False, allow_unicode=True)
