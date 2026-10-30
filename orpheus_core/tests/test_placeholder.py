"""占位接口、TODO 持久化/发现与执行双路径。"""

from __future__ import annotations

import copy
import json
import struct
import uuid
import wave
from pathlib import Path

import pytest
import yaml
from click.testing import CliRunner
from fastapi.testclient import TestClient

from orpheus_core.cli import cli
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.placeholder import COMPONENT_ID, placeholder_ports, validate_routes
from orpheus_core.project import ProjectLoader, project_to_dict
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app
from orpheus_core.server.bridge_process_session import ProcessBridgeSession
from orpheus_core.subgraph import flatten_project
from orpheus_core.todos import collect_todos

ROOT = Path(__file__).resolve().parents[2]
TODO = {
    "id": "filter", "title": "抽取滤波算法", "status": "pending",
    "source_ref": "analysis/index.yaml#filter; model.c:filter",
    "next_step": "比较系数顺序与状态更新", "acceptance": "跨块 impulse golden 通过",
}


@pytest.fixture()
def registry():
    result = Registry([ROOT / "components"])
    result.scan()
    return result


def document():
    return {
        "version": "0.1.0", "sample_rate": 48000, "block_size": 32,
        "graph": {
            "nodes": [
                {"id": "input", "component": "orpheus.builtin.signal_gen", "params": {"channels": 2}},
                {"id": "pending", "component": COMPONENT_ID, "todos": [copy.deepcopy(TODO)]},
                {"id": "sink", "component": "orpheus.builtin.null_sink", "params": {"channels": 2}},
            ],
            "connections": [
                {"from": "input:out", "to": "pending:in"},
                {"from": "pending:out", "to": "sink:in"},
            ],
        },
    }


def load(tmp_path, registry, doc):
    path = tmp_path / "project.yaml"
    path.write_text(yaml.safe_dump(doc, allow_unicode=True), encoding="utf-8")
    return ProjectLoader(registry).load(path)


def test_default_compiles_with_explicit_warning_and_roundtrips(tmp_path, registry):
    project = load(tmp_path, registry, document())
    plan = GraphCompiler(project.registry).compile(flatten_project(project))
    assert plan.node_configs["pending"]["input_ports"] == ["in"]
    assert plan.node_configs["pending"]["output_ports"] == ["out"]
    assert len(plan.warnings) == 1 and "未实现" in plan.warnings[0]
    saved = project_to_dict(project)
    assert saved["graph"]["nodes"][1]["todos"] == [TODO]
    # 替换组件不能丢失下一步与验收索引。
    saved["graph"]["nodes"][1]["component"] = "orpheus.builtin.gain"
    assert collect_todos(load(tmp_path, registry, saved))["items"][0]["source_ref"] == TODO["source_ref"]


@pytest.mark.parametrize("ports,routes,match", [
    ([{"id": "x", "direction": "input", "channels": 1}, {"id": "x", "direction": "output", "channels": 1}], "0", "重复"),
    ([{"id": "in", "direction": "input", "channels": True}], "", "整数"),
    ([{"id": "out", "direction": "output", "channels": 1}], "0", "越界"),
    ([{"id": "out", "direction": "output", "channels": 1}], "", "每个输出"),
    ([{"id": "in", "direction": "input", "channels": 2}, {"id": "out", "direction": "output", "channels": 1}], "0", "通道不匹配"),
    ([{"id": "out", "direction": "output", "channels": 1, "type": "event"}], "-1", "只支持"),
])
def test_reject_ambiguous_interface_and_routes(ports, routes, match):
    with pytest.raises(ValueError, match=match):
        params = {"interface": json.dumps(ports), "routes": routes}
        validate_routes(params, placeholder_ports(params))


def test_compiler_reports_invalid_placeholder(tmp_path, registry):
    doc = document()
    doc["graph"]["nodes"][1]["params"] = {"interface": "not-json"}
    with pytest.raises(CompileError, match="节点 pending.*JSON"):
        project = load(tmp_path, registry, doc)
        GraphCompiler(project.registry).compile(flatten_project(project))


def test_unconnected_routed_input_rejected(tmp_path, registry):
    doc = document()
    doc["graph"]["connections"].pop(0)
    project = load(tmp_path, registry, doc)
    with pytest.raises(CompileError, match="透传输入 in 未连接"):
        GraphCompiler(project.registry).compile(flatten_project(project))


def test_todos_cover_unused_subgraphs_done_and_synthetic(tmp_path, registry):
    doc = document()
    doc["graph"]["nodes"][1]["todos"][0]["status"] = "done"
    doc["subcomponents"] = [{
        "id": "unused", "ports": [],
        "graph": {"nodes": [
            {"id": "leaf", "component": "orpheus.builtin.gain", "todos": [{**TODO, "status": "blocked"}]},
        ], "connections": []},
    }]
    project = load(tmp_path, registry, doc)
    report = collect_todos(project)
    assert report["summary"] == {"total": 2, "open": 2, "blocked": 1}
    assert {item["location"] for item in report["items"]} == {
        "main/pending#__placeholder__", "sub:unused/leaf#filter",
    }
    assert collect_todos(project, include_done=True)["summary"]["total"] == 3
    result = CliRunner().invoke(cli, ["--project-root", str(ROOT), "todos", str(tmp_path), "--json", "--check"])
    assert result.exit_code == 1, result.output
    assert json.loads(result.output)["items"] == report["items"]


def test_duplicate_todo_ids_rejected(tmp_path, registry):
    doc = document()
    doc["graph"]["nodes"][1]["todos"].append(copy.deepcopy(TODO))
    with pytest.raises(ValueError, match="TODO id 重复"):
        load(tmp_path, registry, doc)


def test_todo_api_persists_nested_definitions(tmp_path):
    root = tmp_path
    project_dir = root / "workspace" / "demo"
    project_dir.mkdir(parents=True)
    doc = document()
    (project_dir / "project.yaml").write_text(yaml.safe_dump(doc, allow_unicode=True), encoding="utf-8")
    # 本测试只检验持久化/枚举，不需要任何可编译组件。
    with TestClient(create_app(root)) as client:
        response = client.get("/api/projects/demo/todos")
        assert response.status_code == 200, response.text
        assert response.json()["items"][0]["next_step"] == TODO["next_step"]
        loaded = client.get("/api/projects/demo").json()
        loaded["graph"]["nodes"][1]["todos"][0]["status"] = "done"
        assert client.put("/api/projects/demo", json=loaded).status_code == 200
        assert client.get("/api/projects/demo/todos").json()["items"][0]["synthetic"]
        assert client.get("/api/projects/demo/todos?include_done=true").json()["summary"]["total"] == 2


def test_imported_todos_survive_save_reload_and_enumerate_blockers(tmp_path):
    directory = tmp_path / "workspace" / "demo"
    directory.mkdir(parents=True)
    child = directory / "child.component.yaml"
    child.write_text(yaml.safe_dump({
        "id": "test.pending", "version": "1.0.0", "abi_version": 1,
        "package_type": "placeholder", "ports": [],
        "blockers": [{"id": "state", "gate": "state", "reason": "缺失初值"}],
    }, allow_unicode=True), encoding="utf-8")
    composite = directory / "outer.component.yaml"
    composite.write_text(yaml.safe_dump({
        "id": "test.outer", "version": "1.0.0", "abi_version": 1,
        "package_type": "composite", "ports": [], "imports": ["child.component.yaml"],
        "graph": {"nodes": [
            {"id": "leaf", "component": "test.pending", "todos": [TODO]},
        ], "connections": []},
    }, allow_unicode=True), encoding="utf-8")
    (directory / "project.yaml").write_text(yaml.safe_dump({
        "version": "0.1.0", "imports": ["outer.component.yaml"],
        "graph": {"nodes": [], "connections": []},
    }), encoding="utf-8")
    with TestClient(create_app(tmp_path)) as client:
        report = client.get("/api/projects/demo/todos").json()
        assert report["summary"]["total"] == 2
        assert {item["view"] for item in report["items"]} == {"sub:test.outer"}
        doc = client.get("/api/projects/demo").json()
        doc["subcomponents"][0]["graph"]["nodes"][0]["todos"][0]["next_step"] = "确认初值"
        assert client.put("/api/projects/demo", json=doc).status_code == 200
    with TestClient(create_app(tmp_path)) as client:
        assert client.get("/api/projects/demo/todos").json()["items"][0]["next_step"] == "确认初值"
    assert yaml.safe_load(composite.read_text(encoding="utf-8"))["graph"]["nodes"][0]["todos"][0]["next_step"] == "确认初值"


def test_todo_gate_passes_after_real_component_verified(tmp_path, registry):
    doc = document()
    doc["graph"]["nodes"][1]["component"] = "orpheus.builtin.gain"
    doc["graph"]["nodes"][1]["todos"][0]["status"] = "done"
    load(tmp_path, registry, doc)
    result = CliRunner().invoke(cli, ["--project-root", str(ROOT), "todos", str(tmp_path), "--check"])
    assert result.exit_code == 0, result.output


@pytest.mark.parametrize("mode,routes,expected", [
    ("passthrough", "0,-1", [0.25, -0.5]),
    ("silence", "0,-1", [0.0, 0.0]),
])
def test_placeholder_dynamic_generated_byte_identical(mode, routes, expected):
    name = f"placeholder_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            response = client.post("/api/projects", json={"name": name})
            assert response.status_code == 201, response.text
            directory = ROOT / "workspace" / name
            with wave.open(str(directory / "input.wav"), "wb") as wav:
                wav.setnchannels(2)
                wav.setsampwidth(2)
                wav.setframerate(48000)
                wav.writeframes(struct.pack("<hh", 8192, -16384) * 96)
            interface = [
                {"id": "audio", "direction": "input", "channels": 2},
                {"id": "result", "direction": "output", "channels": 2},
                {"id": "unused", "direction": "output", "channels": 1},
            ]
            doc = document()
            doc["graph"]["nodes"][0] = {
                "id": "input", "component": "orpheus.builtin.wav_in",
                "params": {"channels": 2, "file_path": "input.wav"},
            }
            doc["graph"]["nodes"][1]["params"] = {
                "interface": json.dumps(interface), "mode": mode, "routes": routes,
            }
            doc["graph"]["nodes"][2] = {
                "id": "sink", "component": "orpheus.builtin.wav_out",
                "params": {"channels": 2, "file_path": "out.wav"},
            }
            doc["graph"]["connections"] = [
                {"from": "input:out", "to": "pending:audio"},
                {"from": "pending:result", "to": "sink:in"},
            ]
            assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
            compiled = client.post(f"/api/projects/{name}/compile")
            assert compiled.status_code == 200, compiled.text
            assert compiled.json()["warnings"]
            dynamic = client.post(f"/api/projects/{name}/run")
            assert dynamic.status_code == 200 and dynamic.json()["status"] == "ok", dynamic.text
            dynamic_bytes = (directory / "out.wav").read_bytes()
            generated = client.post(f"/api/projects/{name}/run_generated")
            assert generated.status_code == 200 and generated.json()["status"] == "ok", generated.text
            assert (directory / "out.wav").read_bytes() == dynamic_bytes
            with wave.open(str(directory / "out.wav"), "rb") as wav:
                samples = struct.unpack("<hh", wav.readframes(1))
            assert [sample / 32768 for sample in samples] == pytest.approx(expected, abs=1 / 32768)
            assert dynamic.json()["warnings"] and generated.json()["warnings"]
            assert "占位模块" in dynamic.json()["stderr"]
            assert "占位模块" in generated.json()["stderr"]
            manifest = json.loads((directory / "generated" / "orpheus_app_manifest.json").read_text(encoding="utf-8"))
            session = ProcessBridgeSession(
                [str(directory / "generated" / "build" / "orpheus_generated_cli.exe"), "--bridge-stdio"],
                directory, manifest["id_map"],
                expected_id_map_hash=int(manifest["id_map_hash"], 16),
                log_path=directory / "logs" / "placeholder-bridge.log",
                probe_interval=0.0,
            )
            try:
                assert session.identity_verified
                assert session.map_page()["total"] == manifest["id_count"]
            finally:
                session.stop()
        finally:
            client.delete(f"/api/projects/{name}")
