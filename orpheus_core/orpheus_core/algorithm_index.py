"""Strict Algorithm IR validation and deterministic structural projection."""

from __future__ import annotations

import copy
from collections import Counter
import hashlib
import json
import math
import re
import shutil
from pathlib import Path
from typing import Any

import yaml

from orpheus_core import schemas


class AlgorithmIndexError(ValueError):
    pass


class _UniqueKeyLoader(yaml.SafeLoader):
    pass


def _construct_mapping(loader: _UniqueKeyLoader, node: yaml.MappingNode, deep: bool = False):
    mapping: dict[Any, Any] = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise AlgorithmIndexError(f"duplicate YAML key: {key!r}")
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


_UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _construct_mapping
)


def load_algorithm_index(path: Path) -> dict[str, Any]:
    source = Path(path).read_text(encoding="utf-8")
    document = yaml.load(source, Loader=_UniqueKeyLoader)
    if not isinstance(document, dict):
        raise AlgorithmIndexError("algorithm index must be a mapping")
    schemas.validate(document, schemas.load_algorithm_index_schema())
    validate_algorithm_index(document)
    return document


def verify_source_fingerprints(document: dict[str, Any], source_root: Path) -> None:
    root = Path(source_root).resolve()
    for source in document["sources"]:
        relative = Path(source["relative_file"])
        if relative.is_absolute():
            raise AlgorithmIndexError(f"{source['id']}: source path must be relative")
        path = (root / relative).resolve()
        try:
            path.relative_to(root)
        except ValueError as exc:
            raise AlgorithmIndexError(f"{source['id']}: source path escapes root") from exc
        if not path.is_file():
            raise AlgorithmIndexError(f"{source['id']}: source file not found: {relative.as_posix()}")
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != source["sha256"]:
            raise AlgorithmIndexError(
                f"{source['id']}: sha256 mismatch: expected {source['sha256']}, got {digest}"
            )


def _unique(records: list[dict[str, Any]], label: str) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for record in records:
        record_id = record["id"]
        if record_id in result:
            raise AlgorithmIndexError(f"duplicate {label} id: {record_id}")
        result[record_id] = record
    return result


def _endpoint(endpoint: str) -> tuple[str, str]:
    if ":" not in endpoint:
        raise AlgorithmIndexError(f"endpoint must be definition:port: {endpoint}")
    return tuple(endpoint.rsplit(":", 1))  # type: ignore[return-value]


def validate_algorithm_index(document: dict[str, Any]) -> None:
    sources = _unique(document["sources"], "source")
    types = _unique(document["types"], "type")
    runtimes = _unique(document["runtimes"], "runtime")
    tasks = _unique(document["tasks"], "task")
    definitions = _unique(document["definitions"], "definition")
    buses = _unique(document["buses"], "bus")
    partitions = _unique(document["parameter_partitions"], "parameter partition")
    _unique(document["relations"], "relation")

    component_ids: set[str] = set()
    for definition in definitions.values():
        component_id = definition["component_id"]
        if component_id in component_ids:
            raise AlgorithmIndexError(f"duplicate component_id: {component_id}")
        component_ids.add(component_id)
        if definition["runtime_ref"] not in runtimes:
            raise AlgorithmIndexError(f"{definition['id']}: unknown runtime_ref")
        if not set(definition["task_refs"]) <= set(tasks):
            raise AlgorithmIndexError(f"{definition['id']}: unknown task_refs")
        if not set(definition["source_refs"]) <= set(sources):
            raise AlgorithmIndexError(f"{definition['id']}: unknown source_refs")
        if not set(definition.get("parameter_refs", [])) <= set(partitions):
            raise AlgorithmIndexError(f"{definition['id']}: unknown parameter_refs")
        parent = definition.get("parent")
        if parent is not None and parent not in definitions:
            raise AlgorithmIndexError(f"{definition['id']}: unknown parent {parent}")
        if not set(definition["children"]) <= set(definitions):
            raise AlgorithmIndexError(f"{definition['id']}: unknown child")
        expected_children = {
            child["id"] for child in definitions.values() if child.get("parent") == definition["id"]
        }
        if set(definition["children"]) != expected_children:
            raise AlgorithmIndexError(
                f"{definition['id']}: children do not match parent assignments"
            )
        child_by_node = {
            definitions[item]["node_id"]: definitions[item]
            for item in definition["children"]
        }
        port_ids: set[str] = set()
        for direction in ("inputs", "outputs"):
            for port in definition["interface"][direction]:
                if port["id"] in port_ids:
                    raise AlgorithmIndexError(f"{definition['id']}: duplicate port {port['id']}")
                port_ids.add(port["id"])
                if port["type_ref"] not in types:
                    raise AlgorithmIndexError(f"{definition['id']}: unknown port type")
                if definition["children"] and not port.get("maps_to"):
                    raise AlgorithmIndexError(
                        f"{definition['id']}:{port['id']}: composite port missing maps_to"
                    )
                if port.get("maps_to"):
                    child_node, child_port_id = _endpoint(port["maps_to"])
                    child = child_by_node.get(child_node)
                    if child is None:
                        raise AlgorithmIndexError(
                            f"{definition['id']}:{port['id']}: boundary is not a direct child"
                        )
                    child_ports = child["interface"][direction]
                    child_port = next(
                        (candidate for candidate in child_ports if candidate["id"] == child_port_id),
                        None,
                    )
                    if child_port is None or child_port["type_ref"] != port["type_ref"]:
                        raise AlgorithmIndexError(
                            f"{definition['id']}:{port['id']}: boundary port/type mismatch"
                        )
        for edge in definition["body"]["edges"]:
            if edge["type_ref"] not in types:
                raise AlgorithmIndexError(f"{definition['id']}: edge has unknown type")
            source_node, source_port_id = _endpoint(edge["from"])
            target_node, target_port_id = _endpoint(edge["to"])
            source = child_by_node.get(source_node)
            target = child_by_node.get(target_node)
            if source is None or target is None:
                raise AlgorithmIndexError(
                    f"{definition['id']}: edge endpoint is not a direct child"
                )
            source_port = next(
                (port for port in source["interface"]["outputs"] if port["id"] == source_port_id),
                None,
            )
            target_port = next(
                (port for port in target["interface"]["inputs"] if port["id"] == target_port_id),
                None,
            )
            if source_port is None or target_port is None:
                raise AlgorithmIndexError(f"{definition['id']}: edge port does not exist")
            if source_port["type_ref"] != edge["type_ref"] or target_port["type_ref"] != edge["type_ref"]:
                raise AlgorithmIndexError(f"{definition['id']}: edge type mismatch")

    for definition in definitions.values():
        seen: set[str] = set()
        current = definition
        while current.get("parent") is not None:
            if current["id"] in seen:
                raise AlgorithmIndexError(f"definition cycle at {current['id']}")
            seen.add(current["id"])
            current = definitions[current["parent"]]

    for task in tasks.values():
        if task["runtime_ref"] not in runtimes:
            raise AlgorithmIndexError(f"{task['id']}: unknown runtime_ref")
        for call in task["ordinal_calls"]:
            if call not in definitions:
                raise AlgorithmIndexError(f"{task['id']}: unknown call {call}")

    for bus in buses.values():
        if bus["producer_runtime"] not in runtimes or bus["consumer_runtime"] not in runtimes:
            raise AlgorithmIndexError(f"{bus['id']}: unknown runtime")
        occupied: set[int] = set()
        for field in bus["fields"]:
            type_info = types.get(field["type_ref"])
            if type_info is None:
                raise AlgorithmIndexError(f"{bus['id']}:{field['id']}: unknown type")
            words = math.prod(type_info["shape"])
            start = field["offset_words"]
            span = set(range(start, start + words))
            if occupied & span:
                raise AlgorithmIndexError(f"{bus['id']}: overlapping field {field['id']}")
            if start + words > bus["total_words"]:
                raise AlgorithmIndexError(f"{bus['id']}: field exceeds total_words")
            occupied.update(span)
            for key in ("producer_definition", "consumer_definition"):
                if field.get(key) and field[key] not in definitions:
                    raise AlgorithmIndexError(f"{bus['id']}:{field['id']}: unknown {key}")
        if occupied != set(range(bus["total_words"])):
            raise AlgorithmIndexError(f"{bus['id']}: fields do not cover all words")

    for partition in partitions.values():
        if not set(partition["source_refs"]) <= set(sources):
            raise AlgorithmIndexError(f"{partition['id']}: unknown source_refs")
        if not set(partition["owner_refs"]) <= set(definitions):
            raise AlgorithmIndexError(f"{partition['id']}: unknown owner_refs")
        for owner in partition["owner_refs"]:
            if partition["id"] not in definitions[owner].get("parameter_refs", []):
                raise AlgorithmIndexError(
                    f"{partition['id']}: owner {owner} does not reference partition"
                )

    roots = set(document["system"]["roots"])
    if not roots <= set(definitions):
        raise AlgorithmIndexError("system contains unknown roots")
    if any(definitions[root].get("parent") is not None for root in roots):
        raise AlgorithmIndexError("system roots must not have parents")

    for relation in document["relations"]:
        if relation["type_ref"] not in types:
            raise AlgorithmIndexError(f"{relation['id']}: unknown type")
        for endpoint in (relation["from"], relation["to"]):
            definition_id, port_id = _endpoint(endpoint)
            definition = definitions.get(definition_id)
            if definition is None:
                raise AlgorithmIndexError(f"{relation['id']}: unknown endpoint {endpoint}")
            ports = definition["interface"]["outputs" if endpoint == relation["from"] else "inputs"]
            if port_id not in {port["id"] for port in ports}:
                raise AlgorithmIndexError(f"{relation['id']}: unknown port {endpoint}")

    coverage = set(document["coverage"]["definition_ids"])
    if coverage != set(definitions):
        missing = sorted(set(definitions) - coverage)
        extra = sorted(coverage - set(definitions))
        raise AlgorithmIndexError(f"coverage mismatch: missing={missing}, extra={extra}")


def _safe_file_id(component_id: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_]+", "_", component_id)


def _manifest_port(port: dict[str, Any], direction: str, types: dict[str, dict[str, Any]]) -> dict[str, Any]:
    type_info = types[port["type_ref"]]
    kind = type_info["kind"]
    manifest_type = kind if kind in {"audio", "control", "bulk"} else "bulk"
    result: dict[str, Any] = {
        "id": port["id"],
        "direction": direction,
        "type": manifest_type,
        "sample_format": "f32" if type_info["dtype"] in {"f32", "complex_f32"} else "i32",
        "channels": type_info["shape"][0],
    }
    if type_info.get("sample_rate_hz"):
        result["sample_rate"] = int(round(type_info["sample_rate_hz"]))
    if type_info.get("block_size"):
        result["block_size"] = type_info["block_size"]
    if port.get("maps_to"):
        result["maps_to"] = port["maps_to"]
    return result


def emit_structural_project(document: dict[str, Any], output_dir: Path) -> None:
    validate_algorithm_index(document)
    output_dir = Path(output_dir)
    if output_dir.exists():
        project_path = output_dir / "project.yaml"
        try:
            existing = yaml.safe_load(project_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, yaml.YAMLError) as exc:
            raise AlgorithmIndexError(
                f"refusing to overwrite unmarked output directory: {output_dir}"
            ) from exc
        projection = existing.get("algorithm_projection") if isinstance(existing, dict) else None
        if not isinstance(projection, dict) or projection.get("index_file") != "algorithm-index.yaml":
            raise AlgorithmIndexError(
                f"refusing to overwrite unmarked output directory: {output_dir}"
            )
        components = output_dir / "components"
        if components.exists():
            shutil.rmtree(components)
        for filename in (
            "project.yaml",
            "algorithm-index.yaml",
            "node-notes.json",
            "coverage-report.json",
            "README.md",
        ):
            (output_dir / filename).unlink(missing_ok=True)
    components_dir = output_dir / "components"
    components_dir.mkdir(parents=True)
    definitions = {item["id"]: item for item in document["definitions"]}
    types = {item["id"]: item for item in document["types"]}

    manifest_paths: dict[str, str] = {}
    for definition in document["definitions"]:
        filename = f"{_safe_file_id(definition['component_id'])}.component.yaml"
        manifest_paths[definition["id"]] = filename

    node_notes: dict[str, str] = {}
    for definition in document["definitions"]:
        children = definition["children"]
        manifest: dict[str, Any] = {
            "id": definition["component_id"],
            "name": definition["name"],
            "category": "外部模型/算法索引",
            "description": definition["purpose"],
            "version": document["schema_version"],
            "abi_version": 1,
            "package_type": "composite" if children else "placeholder",
            "ports": [
                *[_manifest_port(port, "input", types) for port in definition["interface"]["inputs"]],
                *[_manifest_port(port, "output", types) for port in definition["interface"]["outputs"]],
            ],
            "parameters": [],
        }
        if children:
            manifest["imports"] = [manifest_paths[child] for child in children]
            manifest["graph"] = {
                "nodes": [
                    {
                        "id": definitions[child]["node_id"],
                        "component": definitions[child]["component_id"],
                        "position": {"x": 80 + (index % 4) * 260, "y": 80 + (index // 4) * 180},
                    }
                    for index, child in enumerate(children)
                ],
                "connections": [
                    {"from": edge["from"], "to": edge["to"]}
                    for edge in definition["body"]["edges"]
                    if edge["kind"] == "audio"
                ],
            }
        else:
            blockers = copy.deepcopy(definition["implementation"]["blockers"])
            if not blockers:
                blockers = [{"id": "verification", "gate": "golden", "reason": "尚未完成逐模块数值验证"}]
            manifest["blockers"] = blockers
        manifest["algorithm_index"] = {
            "definition_id": definition["id"],
            "tasks": definition["task_refs"],
            "operations": definition["operations"],
            "state": definition["state"],
            "implementation": definition["implementation"],
        }
        path = components_dir / manifest_paths[definition["id"]]
        path.write_text(yaml.safe_dump(manifest, sort_keys=False, allow_unicode=True), encoding="utf-8")
        for child in children:
            child_def = definitions[child]
            node_notes[f"sub:{definition['component_id']}/{child_def['node_id']}"] = (
                f"用途：{child_def['purpose']} 原因：按源代码算法边界独立索引；"
                f"实现状态：{child_def['implementation']['status']}。"
            )

    roots = document["system"]["roots"]
    for root in roots:
        root_definition = definitions[root]
        node_notes[root_definition["node_id"]] = (
            f"用途：{root_definition['purpose']}。双击进入按源码边界组织的算法子系统。"
        )
    root_set = set(roots)
    root_connections = []
    for relation in document["relations"]:
        source_definition, source_port = _endpoint(relation["from"])
        target_definition, target_port = _endpoint(relation["to"])
        if source_definition not in root_set or target_definition not in root_set:
            continue
        root_connections.append({
            "from": f"{definitions[source_definition]['node_id']}:{source_port}",
            "to": f"{definitions[target_definition]['node_id']}:{target_port}",
        })
    project = {
        "version": "1.0.0",
        "metadata": {
            "name": f"{document['model_id']}.full_index_projection",
            "description": "由 Algorithm IR 生成的完整研究图；placeholder 表示尚未完成的算法，禁止用于执行等价声明",
        },
        "sample_rate": 48000,
        "block_size": 32,
        "imports": ["components"],
        "graph": {
            "nodes": [
                {
                    "id": definitions[root]["node_id"],
                    "component": definitions[root]["component_id"],
                    "position": {"x": 120 + index * 420, "y": 160},
                }
                for index, root in enumerate(roots)
            ],
            "connections": root_connections,
        },
        "algorithm_projection": {
            "schema_version": document["schema_version"],
            "index_file": "algorithm-index.yaml",
            "definition_count": len(document["definitions"]),
            "tasks": document["tasks"],
            "parameter_partitions": document["parameter_partitions"],
            "relations": document["relations"],
            "buses": document["buses"],
            "unlowered_edges": [
                {"definition_ref": definition["id"], **edge}
                for definition in document["definitions"]
                for edge in definition["body"]["edges"]
                if edge["kind"] != "audio"
            ],
            "unresolved": document["coverage"]["unresolved"],
            "executable": False,
        },
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "project.yaml").write_text(
        yaml.safe_dump(project, sort_keys=False, allow_unicode=True), encoding="utf-8"
    )
    (output_dir / "algorithm-index.yaml").write_text(
        yaml.safe_dump(document, sort_keys=False, allow_unicode=True, width=120),
        encoding="utf-8",
    )
    (output_dir / "node-notes.json").write_text(
        json.dumps(node_notes, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    leaves = [definition for definition in document["definitions"] if not definition["children"]]
    coverage_report = {
        "schema_version": document["schema_version"],
        "definitions": len(document["definitions"]),
        "composites": len(document["definitions"]) - len(leaves),
        "leaves": len(leaves),
        "implementation_status": dict(sorted(Counter(
            definition["implementation"]["status"] for definition in leaves
        ).items())),
        "blocker_gates": dict(sorted(Counter(
            blocker["gate"]
            for definition in leaves
            for blocker in definition["implementation"]["blockers"]
        ).items())),
        "unresolved": document["coverage"]["unresolved"],
        "executable": False,
    }
    (output_dir / "coverage-report.json").write_text(
        json.dumps(coverage_report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (output_dir / "README.md").write_text(
        "# Algorithm IR 全量研究图\n\n"
        f"本工程由 `algorithm-index.yaml` 确定性生成，共 {len(document['definitions'])} 个递归算法定义、"
        f"{len(document['tasks'])} 个任务和 {len(document['parameter_partitions'])} 个参数分区。\n\n"
        "双击 Runtime 和子系统可递归查看音频拓扑。control/message/spectrum/state 边无法投影为普通音频线，"
        "完整记录保存在 `project.yaml` 的 `algorithm_projection` 与原始 IR 中。\n\n"
        "叶节点为 placeholder 时表示算法尚未完成 golden 或运行时能力仍缺失，编译器会主动拒绝；"
        "这张图用于完整分析和逐模块替换，不得宣称可执行或数值等价。\n",
        encoding="utf-8",
    )