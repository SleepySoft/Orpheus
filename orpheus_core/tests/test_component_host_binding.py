"""宿主端口契约在独立声明工具与图编译之间保持一致。"""

import copy
import json
import subprocess

import jsonschema
import pytest

from orpheus_core.component_codegen import check_generated, generate_component
from orpheus_core.component_definition import ComponentDefinitionError, normalize_component_definition
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_component_codegen import PYTHON, ROOT, package, raw, save


@pytest.fixture
def host_raw(raw):
    interfaces = raw["interfaces"]
    interfaces.update({
        "mode": {"type": "int", "default": 0, "range": [0, 1], "update_policy": "restart_required"},
        "ready": {"type": "bool", "default": False, "access": "read_write"},
        "valid": {"type": "bool", "kind": "probe", "access": "read"},
        "audio_storage": {
            "type": "string", "default": "", "shape": ["config:frames", 2],
            "bulk": {"sample_format": "f32", "capacity": 64, "storage": "algorithm.samples",
                     "slot_key": "samples", "double_bank": True},
        },
    })
    interfaces["in"]["host_binding"] = {
        "slot": "samples", "access": "write", "status": "ready", "when": {"mode": 0}}
    interfaces["out"]["host_binding"] = {
        "slot": "samples", "access": "read", "status": "valid", "when": {"mode": 1}}
    return raw


def test_host_binding_schema_independent_cli_and_normalization(package, host_raw):
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(host_raw, schema)
    before = copy.deepcopy(host_raw)
    normalized = normalize_component_definition(host_raw)
    assert host_raw == before
    assert normalized["ports"][0]["host_binding"] == host_raw["interfaces"]["in"]["host_binding"]
    assert normalized["bulk_slots"][0]["id"] == "samples"
    assert normalized["bulk_bindings"][0]["id"] == "audio_storage"
    path = save(package, host_raw)
    for script in ("validate_component.py", "generate_component.py"):
        result = subprocess.run(
            [str(PYTHON), "-X", "utf8", str(ROOT / "scripts" / script), str(path), "--format", "json"],
            cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert json.loads(result.stdout)["ok"] is True
    check_generated(path)


@pytest.mark.parametrize("port,field,value,reason", [
    ("in", "host_binding", None, "slot/access/status"),
    ("in", "host_binding", {}, "slot/access/status"),
    ("in", "type", "bulk", "f32 音频"),
    ("in", "sample_format", "i16", "f32 音频"),
])
def test_host_binding_rejects_malformed_port(host_raw, port, field, value, reason):
    host_raw["interfaces"][port][field] = value
    with pytest.raises(ComponentDefinitionError, match=reason):
        normalize_component_definition(host_raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(host_raw, schema)


@pytest.mark.parametrize("change,reason", [
    ({"slot": "audio_storage"}, "BULK"),
    ({"slot": "missing", "when": {"mode": 1}}, "BULK"),
    ({"status": "missing"}, "bool"),
    ({"status": "channels"}, "bool"),
    ({"status": "valid"}, "不可写"),
    ({"access": "read_write"}, "write/read"),
    ({"when": {"ready": False}}, "重启生效"),
    ({"when": {"mode": True}}, "类型或范围"),
    ({"when": {"mode": 2}}, "类型或范围"),
    ({"when": {"mode": "0"}}, "类型或范围"),
    ({"when": []}, "when"),
    ({"extra": 1}, "未知字段"),
])
def test_host_binding_rejects_invalid_contract_even_when_inactive(host_raw, change, reason):
    host_raw["interfaces"]["in"]["host_binding"].update(change)
    with pytest.raises(ComponentDefinitionError, match=reason):
        normalize_component_definition(host_raw)


@pytest.mark.parametrize("resource_changes,status_changes,reason", [
    ({"access": "read"}, {}, "存储访问权限"),
    ({}, {"count": 1, "default": [False]}, "bool 标量"),
    ({}, {"readonly": True}, "不可写"),
    ({}, {"access": "write"}, "不可写"),
])
def test_host_binding_rejects_unavailable_storage_or_status(host_raw, resource_changes, status_changes, reason):
    host_raw["interfaces"]["audio_storage"].update(resource_changes)
    host_raw["interfaces"]["ready"].update(status_changes)
    with pytest.raises(ComponentDefinitionError, match=reason):
        normalize_component_definition(host_raw)


@pytest.mark.parametrize("parameter,valid,invalid", [
    ({"type": "bool", "default": False}, False, 0),
    ({"type": "string", "default": "ok", "capacity": 4}, "ok", "four"),
    ({"type": "float", "default": 0.5, "range": [0.0, 1.0]}, 0.75, float("nan")),
])
def test_host_binding_conditions_use_strict_scalar_types(host_raw, parameter, valid, invalid):
    parameter["update_policy"] = "restart_required"
    host_raw["interfaces"]["gate"] = parameter
    binding = host_raw["interfaces"]["in"]["host_binding"]
    binding["when"]["gate"] = valid
    normalize_component_definition(host_raw)
    binding["when"]["gate"] = invalid
    with pytest.raises(ComponentDefinitionError, match="类型或范围"):
        normalize_component_definition(host_raw)


def test_host_source_output_can_bind_host_write_storage(host_raw):
    interfaces = host_raw["interfaces"]
    interfaces["out"]["host_binding"] = interfaces["in"].pop("host_binding")
    interfaces["audio_storage"]["bulk"]["slot_key"] = "samples.active"
    interfaces["out"]["host_binding"]["slot"] = "samples.active"
    normalized = normalize_component_definition(host_raw)
    assert normalized["ports"][1]["host_binding"]["access"] == "write"
    assert normalized["bulk_slots"][0]["id"] == "samples.active"


def test_host_binding_rejects_currently_inactive_bulk_storage(package, host_raw):
    host_raw["interfaces"]["audio_storage"]["bulk"]["when"] = {"mode": 0}
    path = save(package, host_raw)
    generate_component(path)
    registry = Registry([ROOT / "components"])
    registry.scan()
    registry.add_manifest(path)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.buffer_source"),
        "host": Node("host", host_raw["id"], params={"mode": 1}),
    }, connections=[Connection(PortRef("source", "out"), PortRef("host", "in"))]))
    with pytest.raises(CompileError, match="未启用"):
        GraphCompiler(registry).compile(project)


@pytest.mark.parametrize("mode,access,status,port", [(0, "write", "ready", "in"), (1, "read", "valid", "out")])
def test_host_binding_survives_registry_and_compilation(package, host_raw, mode, access, status, port):
    path = save(package, host_raw)
    generate_component(path)
    registry = Registry([ROOT / "components"])
    registry.scan()
    registry.add_manifest(path)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.buffer_source"),
        "host": Node("host", host_raw["id"], params={"mode": mode}),
    }, connections=[Connection(PortRef("source", "out"), PortRef("host", "in"))]))
    plan = GraphCompiler(registry).compile(project)
    assert plan.node_configs["host"]["host_ports"] == [{
        "port": port, "slot": "samples", "status": status, "access": access,
        "connected": mode == 0, "channels": 2, "frames": 8, "sample_format": "f32",
    }]
    entry = next(item for item in plan.id_map if item["node"] == "host" and item["key"] == "samples")
    assert entry["count"] == 16 and entry["type"] == "float"
    registry.get(host_raw["id"]).manifest["ports"][0]["host_binding"]["when"] = {"mode": True}
    with pytest.raises(CompileError, match="类型或范围"):
        GraphCompiler(registry).compile(project)
