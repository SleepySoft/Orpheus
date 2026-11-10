"""递归端口事实源、编辑态诊断及只读快照 API。"""

import copy
from pathlib import Path

import pytest
import yaml
import jsonschema
from editor_client import EditorClient as TestClient

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.interfaces import preview_interfaces
from orpheus_core.project import (
    Connection, Graph, Node, PortRef, Project, ProjectLoader, SubParameter,
    SubPort, Subcomponent, Task, project_to_dict,
)
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app
from orpheus_core.server.manager import ProjectManager
from orpheus_core import schemas
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def registry():
    registry = Registry([ROOT / "components"])
    registry.scan()
    return registry


def connection(source, target):
    return Connection(PortRef.parse(source), PortRef.parse(target))


def nested_project():
    inner = Subcomponent(
        id="inner",
        ports=[SubPort("samples", "input", "body:in"), SubPort("out", "output", "body:out")],
        public_parameters=[SubParameter("channels", "input", "body:channels", default=3)],
        graph=Graph(nodes={"body": Node("body", "orpheus.builtin.gain")}),
    )
    outer = Subcomponent(
        id="outer",
        ports=[SubPort("input", "input", "child:samples"), SubPort("output", "output", "child:out")],
        public_parameters=[SubParameter("width", "input", "child:channels", default=5)],
        graph=Graph(nodes={"child": Node("child", "inner", params={"channels": 4})}),
    )
    return Project(
        block_size=96, sample_rate=32000,
        subcomponents=[inner, outer],
        graph=Graph(nodes={"a": Node("a", "outer", params={"width": 7}),
                           "b": Node("b", "outer")}),
    )


def test_recursive_instance_defaults_origins_and_no_mutation(registry):
    project = nested_project()
    before = copy.deepcopy(project_to_dict(project))
    result = preview_interfaces(project, registry)
    assert result["diagnostics"] == []
    for node, channels in [("a", 7), ("b", 5)]:
        port = result["nodes"][node]["ports"][0]
        assert (port["sample_format"], port["channels"], port["frames"], port["sample_rate"]) == (
            "f32", channels, 96, 32000,
        )
        assert port["resolution"]["endpoint"] == f"{node}__child__body:in"
        assert port["resolution"]["component"] == "orpheus.builtin.gain"
        assert "host_binding" not in port
    assert project_to_dict(project) == before
    assert not project.interface_bindings


def test_definition_view_uses_its_own_defaults_not_an_arbitrary_instance(registry):
    project = nested_project()
    result = preview_interfaces(project, registry, "sub:outer")
    assert result["nodes"]["child"]["ports"][0]["channels"] == 5
    inner = preview_interfaces(project, registry, "sub:inner")
    assert inner["nodes"]["body"]["ports"][0]["channels"] == 3


def test_atomic_defaults_and_task_context_without_a_clock(registry):
    project = Project(tasks={"default": Task("default", block_size=48, sample_rate=44100)},
                      graph=Graph(nodes={"gain": Node("gain", "orpheus.builtin.gain")}))
    result = preview_interfaces(project, registry)
    port = result["nodes"]["gain"]["ports"][0]
    assert (port["channels"], port["frames"], port["sample_rate"]) == (2, 48, 44100)


def test_preview_matches_compiled_port_signatures_and_propagation(registry):
    project = Project(
        tasks={"default": Task("default", block_size=32, sample_rate=48000)},
        graph=Graph(nodes={
            "src": Node("src", "orpheus.builtin.signal_gen", params={"channels": 2}),
            "batch": Node("batch", "orpheus.builtin.downrate", params={"factor": 3, "channels": 2}),
            "convert": Node("convert", "orpheus.builtin.resample", params={"factor": 2, "channels": 2}),
            "gain": Node("gain", "orpheus.builtin.gain"),
        }, connections=[connection("src:out", "batch:in"), connection("batch:out", "convert:in"),
                        connection("convert:out", "gain:in")]),
    )
    result = preview_interfaces(project, registry)
    assert result["diagnostics"] == []
    plan = GraphCompiler(registry).compile(flatten_project(project, registry))
    for node, resolved in result["nodes"].items():
        for port in resolved["ports"]:
            signature = plan.node_configs[node]["port_signatures"][port["id"]]
            assert {key: port[key] for key in signature} == signature
    assert result["nodes"]["gain"]["ports"][1]["frames"] == 96
    assert result["nodes"]["gain"]["ports"][1]["sample_rate"] == 24000
    for buffer in plan.buffers.values():
        node, port = buffer["to"].split(":")
        assert plan.node_configs[node]["port_signatures"][port]["frames"] == buffer["frame_count"]


@pytest.mark.parametrize("component,frames,channels,params", [
    ("circular_buffer", 384, 2, {"frame_size": 256, "hop_size": 128, "num_frames": 3}),
    ("circular_buffer", 128, 2, {"frame_size": 256, "hop_size": 128, "num_frames": 1}),
    ("window", 256, 10, {"window_size": 256}),
    ("window", 256, 2, {"window_size": 256}),
])
def test_automatic_input_signature_matches_buffer_through_recursive_aliases(
    registry, component, frames, channels, params,
):
    project = nested_project()
    project.block_size = 32
    body = project.subcomponents[0].graph.nodes["body"]
    body.component = f"orpheus.builtin.{component}"
    body.params = params
    project.subcomponents[1].ports[0].signature = {"block_size": frames}
    project.graph = Graph(nodes={
        "clock": Node("clock", "orpheus.builtin.signal_gen", params={"channels": channels}),
        "batch": Node("batch", "orpheus.builtin.downrate",
                      params={"channels": channels, "factor": frames // 32}),
        "a": Node("a", "outer", params={"width": channels}),
    }, connections=[connection("clock:out", "batch:in"), connection("batch:out", "a:input")])
    before = project_to_dict(project)
    result = preview_interfaces(project, registry)
    assert result["diagnostics"] == []
    assert result["nodes"]["a"]["ports"][0]["frames"] == frames
    flat = flatten_project(project, registry)
    plan = GraphCompiler(registry).compile(flat)
    endpoint = "a__child__body:in"
    buffer = next(buffer for buffer in plan.buffers.values() if buffer["to"] == endpoint)
    cfg = plan.node_configs["a__child__body"]
    assert cfg["port_signatures"]["in"]["frames"] == buffer["frame_count"] == frames
    assert cfg["frames"] == frames
    assert plan.node_configs["clock"]["port_signatures"]["out"]["frames"] == 32
    assert project_to_dict(project) == before


def test_automatic_input_is_resolved_per_connection_not_per_node(registry):
    project = Project(block_size=32, graph=Graph(nodes={
        "clock": Node("clock", "orpheus.builtin.signal_gen", params={"channels": 1}),
        "batch": Node("batch", "orpheus.builtin.downrate", params={"factor": 3, "channels": 1}),
        "mixer": Node("mixer", "orpheus.builtin.mixer", params={"channels": 1}),
    }, connections=[connection("clock:out", "batch:in"), connection("batch:out", "mixer:in0")]))
    plan = GraphCompiler(registry).compile(flatten_project(project, registry))
    signatures = plan.node_configs["mixer"]["port_signatures"]
    assert signatures["in0"]["frames"] == 96
    assert signatures["in1"]["frames"] == 32
    assert signatures["out"]["frames"] == 96


def test_automatic_input_resolution_preserves_explicit_port_constraints(registry):
    local = registry.fork()
    manifest = local.get("orpheus.builtin.gain").manifest
    manifest["ports"][0]["block_size"] = 64
    project = Project(block_size=32, graph=Graph(nodes={
        "clock": Node("clock", "orpheus.builtin.signal_gen", params={"channels": 2}),
        "batch": Node("batch", "orpheus.builtin.downrate", params={"factor": 3, "channels": 2}),
        "gain": Node("gain", "orpheus.builtin.gain"),
    }, connections=[connection("clock:out", "batch:in"), connection("batch:out", "gain:in")]))
    signatures, _ = GraphCompiler(local).resolve_port_signatures(project)
    assert signatures["gain:in"]["frames"] == 64
    assert signatures["gain:out"]["frames"] == 96


@pytest.mark.parametrize("field,value,reported,message", [
    ("channels", 1, "channels", "channels mismatch"),
    ("sample_format", "i16", "sample_format", "format mismatch"),
    ("block_size", 64, "frames", "block size mismatch"),
    ("sample_rate", 44100, "sample_rate", "sample rate mismatch"),
    ("type", "bulk", "type", "type mismatch"),
])
def test_configured_connection_mismatch_is_reported_without_rewriting(
    registry, field, value, reported, message,
):
    local = registry.fork()
    local.get("orpheus.builtin.gain").manifest["ports"][0][field] = value
    project = Project(graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.signal_gen", params={"channels": 2}),
        "gain": Node("gain", "orpheus.builtin.gain", params={"channels": 2}),
        "ok": Node("ok", "orpheus.builtin.null_sink", params={"channels": 2}),
    }, connections=[connection("source:out", "gain:in"), connection("source:out", "ok:in")]))
    before = copy.deepcopy(project_to_dict(project))
    result = preview_interfaces(project, local)
    target = result["nodes"]["gain"]["ports"][0]
    assert target[reported] == value
    edge = next(edge for edge in result["connections"] if edge["to"] == "gain:in")
    assert edge["status"] == "mismatch"
    assert reported in {item["field"] for item in edge["differences"]}
    assert next(edge for edge in result["connections"] if edge["to"] == "ok:in")["status"] == "matched"
    assert project_to_dict(project) == before
    with pytest.raises(CompileError, match=message):
        GraphCompiler(local).compile(flatten_project(project, local))


@pytest.mark.parametrize("configured,expected,status", [
    (48000, 48000, "mismatch"), (0, 24000, "matched"), (0.0, 24000, "matched"),
])
def test_wav_rate_only_adapts_when_explicitly_automatic(registry, configured, expected, status):
    project = Project(graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.signal_gen", params={"channels": 2}),
        "convert": Node("convert", "orpheus.builtin.resample", params={"factor": 2, "channels": 2}),
        "sink": Node("sink", "orpheus.builtin.wav_out",
                     params={"channels": 2, "sample_rate": configured}),
    }, connections=[connection("source:out", "convert:in"), connection("convert:out", "sink:in")]))
    result = preview_interfaces(project, registry)
    assert result["nodes"]["sink"]["ports"][0]["sample_rate"] == expected
    edge = next(edge for edge in result["connections"] if edge["to"] == "sink:in")
    assert edge["status"] == status
    assert project.graph.nodes["sink"].params["sample_rate"] == configured
    if configured:
        with pytest.raises(CompileError, match="sample rate mismatch"):
            GraphCompiler(registry).compile(flatten_project(project, registry))
    else:
        plan = GraphCompiler(registry).compile(flatten_project(project, registry))
        assert int(plan.node_configs["sink"]["params"]["sample_rate"]) == expected


def test_recursive_connection_reports_visible_endpoints_and_preserves_instance_configuration(registry):
    project = nested_project()
    project.graph.connections = [connection("a:output", "b:input")]
    result = preview_interfaces(project, registry)
    edge = result["connections"][0]
    assert (edge["from"], edge["to"], edge["status"]) == ("a:output", "b:input", "mismatch")
    assert next(item for item in edge["differences"] if item["field"] == "channels") == {
        "field": "channels", "source": 7, "target": 5,
        "message": "channels mismatch: a__child__body:out (7) -> b__child__body:in (5)",
    }
    assert result["nodes"]["a"]["ports"][1]["channels"] == 7
    assert result["nodes"]["b"]["ports"][0]["channels"] == 5
    assert project.graph.nodes["a"].params == {"width": 7}
    assert project.graph.nodes["b"].params == {}


@pytest.mark.parametrize("signature", [
    {"channels": 9}, {"sample_format": "s16"}, {"block_size": 24}, {"sample_rate": 44100},
])
def test_explicit_composite_fields_are_constraints_in_preview_and_compile(registry, signature):
    project = nested_project()
    project.subcomponents[1].ports[0].signature = signature
    result = preview_interfaces(project, registry)
    port = result["nodes"]["a"]["ports"][0]
    assert port["resolution"]["status"] == "conflict"
    assert port["channels"] == 7
    assert "a:input" in port["resolution"]["message"]
    assert "a__child__body:in" in port["resolution"]["message"]
    with pytest.raises(CompileError, match="不一致"):
        GraphCompiler(registry).compile(flatten_project(project, registry))


def test_explicit_constraints_resolve_outer_parameters_and_roundtrip(registry):
    project = nested_project()
    project.subcomponents[1].ports[0].signature = {
        "channels": "param:width", "block_size": "task:block_size", "format": "f32",
    }
    data = project_to_dict(project)
    loaded = ProjectLoader(registry).load_document(data, ROOT / "workspace" / "preview" / "project.yaml")
    result = preview_interfaces(loaded, loaded.registry)
    assert result["diagnostics"] == []
    assert project_to_dict(loaded)["subcomponents"][1]["ports"][0]["channels"] == "param:width"
    assert "resolution" not in str(project_to_dict(loaded))


def test_fractional_rate_constraints_compare_exact_values_not_json_float_rounding(registry):
    project = nested_project()
    body = project.subcomponents[0].graph.nodes["body"]
    body.component = "orpheus.builtin.rational_resample"
    body.params.update({"up": 1, "down": 3})
    project.subcomponents[1].ports[1].signature = {"sample_rate": "task:sample_rate/3"}
    result = preview_interfaces(project, registry)
    assert result["diagnostics"] == []
    assert result["nodes"]["a"]["ports"][1]["sample_rate"] == 32000 / 3
    assert result["nodes"]["a"]["ports"][1]["frames"] == 32


@pytest.mark.parametrize("invalid", ["missing_component", "missing_port", "cycle", "division", "task"])
def test_invalid_component_does_not_hide_unrelated_signatures(registry, invalid):
    project = nested_project()
    project.graph.nodes = {"bad": Node("bad", "outer"), "good": Node("good", "orpheus.builtin.gain")}
    if invalid == "missing_component":
        project.subcomponents[0].graph.nodes["body"].component = "missing"
    elif invalid == "missing_port":
        project.subcomponents[0].ports[0].maps_to = "body:absent"
    elif invalid == "cycle":
        project.subcomponents[0].graph.nodes["body"].component = "outer"
    elif invalid == "division":
        project.subcomponents[0].graph.nodes["body"].component = "orpheus.builtin.resample"
        project.subcomponents[0].graph.nodes["body"].params["factor"] = 0
    else:
        project.subcomponents[0].graph.nodes["body"].task = "absent"
    result = preview_interfaces(project, registry)
    assert result["nodes"]["bad"]["status"] == "error"
    assert result["nodes"]["bad"]["message"]
    assert result["nodes"]["good"]["status"] == "resolved"
    assert result["diagnostics"][0]["nodes"] == ["bad"]


def test_variable_ports_use_manifest_defaults(registry):
    project = Project(graph=Graph(nodes={
        "pins": Node("pins", "orpheus.builtin.deinterleave", params={"channels": 3}),
    }))
    result = preview_interfaces(project, registry)
    assert [port["id"] for port in result["nodes"]["pins"]["ports"]] == ["in", "out0", "out1", "out2"]


def test_disconnected_nodes_share_the_resolved_task_clock_context(registry):
    project = Project(sample_rate=48000, graph=Graph(nodes={
        "clock": Node("clock", "orpheus.builtin.signal_gen", params={"sample_rate": 44100}),
        "gain": Node("gain", "orpheus.builtin.gain"),
    }))
    result = preview_interfaces(project, registry)
    assert result["nodes"]["gain"]["ports"][0]["sample_rate"] == 44100
    project.graph.connections.append(connection("clock:out", "gain:in"))
    plan = GraphCompiler(registry).compile(flatten_project(project, registry))
    assert result["nodes"]["gain"]["ports"][0]["sample_rate"] == plan.node_configs["gain"]["sample_rate"]


def test_task_clock_conflict_does_not_hide_another_task(registry):
    project = Project(tasks={"default": Task("default"), "other": Task("other", sample_rate=32000)},
                      graph=Graph(nodes={
                          "a": Node("a", "orpheus.builtin.signal_gen", params={"sample_rate": 44100}),
                          "b": Node("b", "orpheus.builtin.signal_gen", params={"sample_rate": 48000}),
                          "good": Node("good", "orpheus.builtin.gain", task="other"),
                      }))
    result = preview_interfaces(project, registry)
    assert result["nodes"]["a"]["status"] == "error"
    assert "disagree" in result["nodes"]["b"]["message"]
    assert result["nodes"]["good"]["ports"][0]["sample_rate"] == 32000


def test_dangling_connection_with_no_remaining_nodes_is_not_silently_accepted(registry):
    project = Project(graph=Graph(connections=[connection("gone:out", "missing:in")]))
    result = preview_interfaces(project, registry)
    assert result["nodes"] == {}
    assert "gone:out" in result["diagnostics"][0]["message"]


def test_expression_counts_multiple_distinct_port_signatures_and_no_inherited_host_binding(tmp_path, registry):
    manifest = {
        "id": "test.signature", "name": "签名表达式夹具", "version": "1.0.0",
        "abi_version": 7,
        "parameters": [
            {"id": "channels", "type": "int", "default": 2, "range": [1, 32],
             "update_policy": "restart_required", "affects_signature": True},
        ],
        "ports": [
        {"id": "in", "direction": "input", "type": "audio", "sample_format": "i16", "channels": 1,
         "block_size": "task:block_size/param:channels", "count": "param:channels*2"},
        {"id": "out", "direction": "output", "type": "audio", "sample_format": "f32",
         "channels": "param:channels", "block_size": "task:block_size*param:channels"},
        ],
    }
    path = tmp_path / "component.yaml"
    path.write_text(yaml.safe_dump(manifest), encoding="utf-8")
    local = registry.fork()
    local.add_manifest(path)
    project = Project(block_size=48, graph=Graph(nodes={"mixed": Node("mixed", "test.signature")}))
    result = preview_interfaces(project, local)
    ports = result["nodes"]["mixed"]["ports"]
    assert [port["id"] for port in ports] == ["in0", "in1", "in2", "in3", "out"]
    assert (ports[0]["sample_format"], ports[0]["channels"], ports[0]["frames"]) == ("i16", 1, 24)
    assert (ports[-1]["sample_format"], ports[-1]["channels"], ports[-1]["frames"]) == ("f32", 2, 96)

    host = Subcomponent("host", ports=[SubPort("samples", "input", "body:in")],
                        graph=Graph(nodes={"body": Node("body", "orpheus.builtin.host_audio")}))
    project = Project(subcomponents=[host], graph=Graph(nodes={"wrapped": Node("wrapped", "host")}))
    result = preview_interfaces(project, registry)
    assert result["nodes"]["wrapped"]["ports"][0]["resolution"]["component"] == "orpheus.builtin.host_audio"
    assert "host_binding" not in result["nodes"]["wrapped"]["ports"][0]


def test_unconnected_input_expressions_use_declared_input_task_context(registry):
    project = Project(graph=Graph(nodes={"convert": Node("convert", "orpheus.builtin.resample")}))
    result = preview_interfaces(project, registry)
    assert result["nodes"]["convert"]["ports"][1]["sample_rate"] == 24000


def test_composite_alias_schema_and_save_do_not_invent_audio_or_f32(registry):
    alias = ProjectManager._merge_composite_manifest({}, {
        "id": "test.alias", "ports": [{"id": "data", "direction": "input", "maps_to": "body:in"}],
    })
    assert alias["ports"] == [{"id": "data", "direction": "input", "maps_to": "body:in"}]
    schema = schemas.load_component_manifest_schema()
    schemas.validate(alias, schema)
    atomic = copy.deepcopy(registry.get("orpheus.builtin.gain").manifest)
    del atomic["ports"][0]["type"]
    with pytest.raises(jsonschema.ValidationError, match="'type'"):
        schemas.validate(atomic, schema)
    del alias["ports"][0]["maps_to"]
    with pytest.raises(jsonschema.ValidationError, match="'maps_to'"):
        schemas.validate(alias, schema)


def test_latest_data_feedback_keeps_compile_equivalent_signatures(registry):
    from test_data_ports import data_project

    local = registry.fork()
    local.add_manifest(ROOT / "orpheus_core" / "tests" / "fixtures" / "data_ports" / "component.yaml")
    project = data_project(latest=True)
    plan = GraphCompiler(local).compile(flatten_project(project, local))
    project.graph.connections.append(connection("consumer:scalar_out", "producer:held"))
    result = preview_interfaces(project, local)
    assert result["diagnostics"] == []
    for node, resolved in result["nodes"].items():
        for port in resolved["ports"]:
            signature = plan.node_configs[node]["port_signatures"][port["id"]]
            assert {key: port[key] for key in signature} == signature
    with pytest.raises(CompileError, match="cycle"):
        GraphCompiler(local).compile(flatten_project(project, local))


def test_preview_api_resolves_unsaved_snapshot_without_writing(tmp_path, registry):
    directory = tmp_path / "workspace" / "preview"
    directory.mkdir(parents=True)
    (directory / "project.yaml").write_text(
        "version: 0.1.0\ngraph: {nodes: [], connections: []}\n", encoding="utf-8",
    )
    # 只复制测试需要的组件定义，服务不依赖共享工作区。
    components = tmp_path / "components" / "gain"
    components.mkdir(parents=True)
    manifest = registry.get("orpheus.builtin.gain").manifest_path
    (components / "component.yaml").write_bytes(manifest.read_bytes())
    before = {path.relative_to(tmp_path): path.read_bytes()
              for path in tmp_path.rglob("*") if path.is_file()}
    with TestClient(create_app(tmp_path)) as client:
        snapshot = project_to_dict(nested_project())
        response = client.post("/api/projects/preview/interfaces",
                               json={"document": snapshot, "view_key": "main"})
        assert response.status_code == 200, response.text
        assert response.json()["nodes"]["a"]["ports"][0]["channels"] == 7
        snapshot["graph"]["nodes"][0]["params"]["width"] = 11
        response = client.post("/api/projects/preview/interfaces", json={"document": snapshot})
        assert response.json()["nodes"]["a"]["ports"][0]["channels"] == 11
        assert client.get("/api/projects/preview").json()["graph"]["nodes"] == []
        assert client.post("/api/projects/preview/interfaces", json={"document": {}}).status_code == 422
        assert client.post("/api/projects/preview/interfaces",
                           json={"document": snapshot, "view_key": "sub:absent"}).status_code == 422
        assert client.post("/api/projects/preview/interfaces",
                           json={"document": snapshot, "view_key": 123}).status_code == 422
    after = {path.relative_to(tmp_path): path.read_bytes()
             for path in tmp_path.rglob("*") if path.is_file()}
    assert after == before
