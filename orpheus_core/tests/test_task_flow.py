"""显式驱动顺序、节拍、复合展开及旧工程兼容。"""

from __future__ import annotations

import copy
from dataclasses import asdict
import json
from pathlib import Path
import struct
import subprocess
import uuid
import wave

import pytest

from orpheus_core import schemas
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import (
    Connection, ControlConnection, DriveConnection, Graph, Node, PortRef,
    Project, ProjectLoader, Subcomponent, project_to_dict,
)
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def registry():
    result = Registry()
    result.add_search_path(ROOT / "components")
    result.scan()
    return result


def drive(src, dst, every=1, phase=None):
    return DriveConnection(PortRef(src, "out"), PortRef(dst, "in"),
                           every, every - 1 if phase is None else phase)


def data(src, dst):
    return Connection(PortRef(src, "out"), PortRef(dst, "in"))


def project():
    result = Project(flow_mode="task_sequence", block_size=32)
    result.graph.nodes = {
        "clock": Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 100}),
        "a": Node("a", "orpheus.builtin.bulk_relay",
                  params={"count": 4, "initial_values": "1,2,3,4"}),
        "b": Node("b", "orpheus.builtin.bulk_relay", params={"count": 4}),
    }
    result.drive_connections = [drive("clock", "a"), drive("a", "b")]
    result.control_connections = [
        ControlConnection(PortRef("a", "values"), PortRef("b", "values")),
    ]
    return result


def test_source_task_has_only_driver_ports_and_before_call_parameters(registry):
    source = project()
    plan = GraphCompiler(registry).compile(source)
    assert plan.flow_mode == "task_sequence"
    assert plan.execution_order == ["clock", "a", "b"]
    assert len(plan.tasks) == 1
    task = plan.tasks[0]
    assert (task["id"], task["source"], task["frequency_hz"]) == ("task_clock", "clock", 100)
    assert task["schedule"] == {
        "tick": 480, "periods": {"clock": 1, "a": 1, "b": 1},
        "phases": {"clock": 0, "a": 0, "b": 0},
    }
    assert plan.buffers == {}
    assert plan.node_configs["clock"]["input_ports"] == []
    assert plan.node_configs["clock"]["output_ports"] == ["out"]
    assert plan.node_configs["clock"]["output_drive_ports"] == {"out": 1}
    assert plan.node_configs["clock"]["trigger_output_ports"] == ["out"]
    assert plan.control_links[0]["delivery"] == "before_call"
    assert source.graph.nodes["a"].task == "default"
    assert source.tasks == {}


def test_driver_order_is_not_node_storage_order(registry):
    source = project()
    source.graph.nodes = dict(reversed(list(source.graph.nodes.items())))
    assert GraphCompiler(registry).compile(source).execution_order == ["clock", "a", "b"]


@pytest.mark.parametrize("every,phase", [(2, 0), (2, 1), (3, 2)])
def test_explicit_decimation_phase(registry, every, phase):
    source = project()
    source.control_connections = []
    source.graph.nodes["divide"] = Node("divide", "orpheus.builtin.trigger_divider",
                                       params={"factor": every, "phase": phase})
    source.drive_connections = [drive("clock", "divide"), drive("divide", "a"), drive("a", "b")]
    plan = GraphCompiler(registry).compile(source)
    assert plan.node_configs["divide"]["params"]["factor"] == every
    assert plan.node_configs["divide"]["params"]["phase"] == phase
    assert plan.tasks[0]["schedule"]["periods"]["b"] == 1
    assert plan.tasks[0]["schedule"]["phases"]["b"] == 0
    assert (plan.node_configs["b"]["period"], plan.node_configs["b"]["phase"]) == (1, 0)
    assert "frame_step" not in plan.node_configs["b"]
    assert "frame_origin" not in plan.node_configs["b"]


def test_nested_every_is_rejected_for_component_owned_delivery(registry):
    source = project()
    source.control_connections = []
    source.drive_connections = [drive("clock", "a", 2, 1), drive("a", "b", 3, 0)]
    with pytest.raises(CompileError, match="连接不能再次分频"):
        GraphCompiler(registry).compile(source)


@pytest.mark.parametrize("mutation,error", [
    (lambda p: p.drive_connections.clear(), "没有驱动来源"),
    (lambda p: p.drive_connections.append(drive("b", "a")), "循环"),
    (lambda p: p.drive_connections.append(drive("clock", "a")), "重复"),
    (lambda p: p.drive_connections.append(drive("missing", "a")), "不存在"),
    (lambda p: setattr(p.drive_connections[0], "every", 0), "连接不能再次分频"),
    (lambda p: setattr(p.drive_connections[0], "phase", 1), "连接不能再次分频"),
    (lambda p: setattr(p.drive_connections[0], "every", True), "every"),
])
def test_invalid_driver_contract_rejected(registry, mutation, error):
    source = project()
    mutation(source)
    with pytest.raises(CompileError, match=error):
        GraphCompiler(registry).compile(source)


def test_parameter_wire_uses_declared_sibling_call_order(registry):
    source = project()
    source.drive_connections = [drive("clock", "a"), drive("clock", "b")]
    plan = GraphCompiler(registry).compile(source)
    assert plan.call_graph["clock"] == [
        {"node": node, "every": 1, "phase": 0, "output_port": 1, "input_port": 0,
         "source_interface": "out", "target_interface": "in"}
        for node in ("a", "b")
    ]


def test_fanout_keeps_direct_calls_without_join_scheduling(registry):
    source = project()
    source.control_connections = []
    source.graph.nodes["c"] = copy.deepcopy(source.graph.nodes["b"])
    source.graph.nodes["c"].id = "c"
    source.drive_connections = [
        drive("clock", "a"), drive("clock", "b"),
        drive("a", "c"), drive("b", "c"),
    ]
    assert GraphCompiler(registry).compile(source).execution_order == ["clock", "a", "c", "b"]
    source.graph.nodes["divide"] = Node("divide", "orpheus.builtin.trigger_divider",
                                       params={"factor": 2})
    source.drive_connections[1:2] = [drive("clock", "divide"), drive("divide", "b")]
    plan = GraphCompiler(registry).compile(source)
    edge = {"node": "c", "every": 1, "phase": 0, "output_port": 1, "input_port": 0,
            "source_interface": "out", "target_interface": "in"}
    assert plan.call_graph["a"] == [edge]
    assert plan.call_graph["b"] == [edge]


def test_two_sources_have_independent_tasks_and_reject_shared_instance(registry):
    source = project()
    source.control_connections = []
    source.graph.nodes["other"] = Node("other", "orpheus.builtin.task_source",
                                      params={"frequency_hz": 200})
    source.drive_connections = [drive("clock", "a"), drive("other", "b")]
    plan = GraphCompiler(registry).compile(source)
    assert [task["source"] for task in plan.tasks] == ["clock", "other"]
    assert [task["frequency_hz"] for task in plan.tasks] == [100, 200]
    assert plan.schedule["multi_clock"] is True
    source.drive_connections.append(drive("other", "a"))
    with pytest.raises(CompileError, match="多个 Source"):
        GraphCompiler(registry).compile(source)


def test_cross_task_audio_remains_an_explicit_async_bridge(registry):
    source = Project(flow_mode="task_sequence", block_size=480)
    source.graph.nodes = {
        "audio": Node("audio", "orpheus.builtin.signal_gen", params={"channels": 1}),
        "g1": Node("g1", "orpheus.builtin.gain", params={"channels": 1}),
        "g2": Node("g2", "orpheus.builtin.gain", params={"channels": 1}),
        "clock": Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 100}),
        "bridge": Node("bridge", "orpheus.builtin.async_bridge", params={"channels": 1}),
        "sink": Node("sink", "orpheus.builtin.wav_out", params={"channels": 1}),
    }
    source.graph.connections = [data("audio", "g1"), data("g1", "g2"),
                                data("g2", "bridge"), data("bridge", "sink")]
    source.drive_connections = [drive("audio", "g1"), drive("g1", "g2"),
                                DriveConnection(PortRef("clock", "out"), PortRef("bridge", "tick")),
                                drive("bridge", "sink")]
    plan = GraphCompiler(registry).compile(source)
    assert plan.node_configs["g2"]["task"] == "task_audio"
    assert plan.node_configs["bridge"]["task"] == "task_clock"
    assert any(buf.get("task_bridge") for buf in plan.buffers.values())


def audio_project():
    source = Project(flow_mode="task_sequence", block_size=32)
    source.graph.nodes = {
        "s": Node("s", "orpheus.builtin.signal_gen", params={"channels": 1}),
        "r": Node("r", "orpheus.builtin.downrate", params={"channels": 1, "factor": 2}),
        "g": Node("g", "orpheus.builtin.gain", params={"channels": 1}),
    }
    source.graph.connections = [data("s", "r"), data("r", "g")]
    source.drive_connections = [drive("s", "r"), drive("r", "g", 2)]
    return source


def test_default_and_declared_output_cadence(registry):
    plan = GraphCompiler(registry).compile(audio_project())
    assert plan.node_configs["s"]["output_cadences"]["out"] == {"period": 1, "phase": 0}
    assert plan.node_configs["r"]["output_cadences"]["out"] == {"period": 2, "phase": 1}
    assert (plan.node_configs["g"]["period"], plan.node_configs["g"]["phase"]) == (2, 1)
    assert plan.node_configs["g"]["frames"] == 64


def test_source_divided_call_drives_consumer_not_buffer_fullness(registry):
    source = audio_project()
    source.drive_connections = [drive("s", "r"), drive("s", "g", 2)]
    plan = GraphCompiler(registry).compile(source)
    assert plan.call_graph["s"] == [
        {"node": "r", "every": 1, "phase": 0},
        {"node": "g", "every": 2, "phase": 1},
    ]
    assert plan.call_graph["r"] == []
    assert plan.node_configs["g"]["frames"] == 64


def test_sibling_chain_signatures_follow_actual_depth_first_calls(registry):
    source = audio_project()
    source.graph.nodes["mid"] = Node("mid", "orpheus.builtin.gain", params={"channels": 1})
    source.graph.connections = [data("s", "r"), data("r", "mid"), data("mid", "g")]
    source.drive_connections = [drive("s", "r"), drive("r", "mid", 2),
                                drive("s", "g", 2)]
    plan = GraphCompiler(registry).compile(source)
    assert plan.execution_order == ["s", "r", "mid", "g"]
    assert plan.node_configs["g"]["frames"] == 64


def test_source_drives_split_at_base_rate_after_divided_producer(registry):
    source = audio_project()
    source.graph.nodes["split"] = Node("split", "orpheus.builtin.block_split",
                                      params={"channels": 1, "factor": 2})
    source.graph.connections.append(data("g", "split"))
    source.drive_connections = [drive("s", "r"), drive("s", "g", 2),
                                drive("s", "split")]
    plan = GraphCompiler(registry).compile(source)
    assert plan.node_configs["g"]["frames"] == 64
    assert plan.node_configs["split"]["frames"] == 32
    assert plan.node_configs["split"]["period"] == 1
    assert plan.call_graph["s"][-1] == {"node": "split", "every": 1, "phase": 0}
    source.drive_connections[-1] = drive("s", "split", 2)
    divided = GraphCompiler(registry).compile(source)
    assert divided.call_graph["s"][-1] == {"node": "split", "every": 2, "phase": 1}
    assert divided.node_configs["split"]["frames"] == 32


def test_declared_window_history_preserves_first_zero_padded_call(registry):
    source = Project(flow_mode="task_sequence", block_size=32)
    source.graph.nodes = {
        "s": Node("s", "orpheus.builtin.signal_gen", params={"channels": 1}),
        "history": Node("history", "orpheus.builtin.circular_buffer",
                        params={"channels": 1, "frame_size": 64, "hop_size": 32, "num_frames": 1}),
        "gain": Node("gain", "orpheus.builtin.gain", params={"channels": 1}),
    }
    source.graph.connections = [data("s", "history"), data("history", "gain")]
    source.drive_connections = [drive("s", "history"), drive("history", "gain")]
    plan = GraphCompiler(registry).compile(source)
    cfg = plan.node_configs["gain"]
    assert (cfg["frames"], cfg["frame_step"], cfg["frame_origin"]) == (64, 32, 0)
    assert cfg["initial_padding_frames"] == 32
    local = registry.fork()
    local.get("orpheus.builtin.circular_buffer").manifest["scheduling"].pop("history")
    with pytest.raises(CompileError, match="启动相位"):
        GraphCompiler(local).compile(source)


def test_bridge_identity_includes_direct_call_order(registry):
    from orpheus_core.bridge.identity import build_identity

    source = project()
    source.control_connections = []
    source.drive_connections = [drive("clock", "a"), drive("clock", "b")]
    plan = GraphCompiler(registry).compile(source)
    before = build_identity(plan)
    plan.call_graph["clock"].reverse()
    after = build_identity(plan)
    assert before["graph_hash"] != after["graph_hash"]
    assert before["plan_hash"] != after["plan_hash"]
    assert before["id_map_hash"] == after["id_map_hash"]


def test_data_binding_does_not_repair_invalid_call_cadence_or_entry(registry):
    source = audio_project()
    source.drive_connections[-1] = drive("r", "g")
    with pytest.raises(CompileError, match="交付周期/相位|启动相位"):
        GraphCompiler(registry).compile(source)
    source.drive_connections = [drive("s", "g"), drive("g", "r")]
    with pytest.raises(CompileError, match="已声明的驱动出口和处理入口"):
        GraphCompiler(registry).compile(source)


def test_interface_outlet_rejects_edge_division_despite_legacy_cadence(registry):
    local = registry.fork()
    info = copy.deepcopy(local.get("orpheus.builtin.gain"))
    info.manifest["ports"][1]["cadence"] = {"period": 2, "phase": 0}
    local._components[info.id] = info
    source = audio_project()
    source.graph.nodes["r"] = Node("r", "orpheus.builtin.gain", params={"channels": 1})
    source.drive_connections[-1] = drive("r", "g", 2, 0)
    with pytest.raises(CompileError, match="连接不能再次分频"):
        GraphCompiler(local).compile(source)


def test_aggregate_cannot_claim_output_before_startup_data_exists(registry):
    source = audio_project()
    source.drive_connections[-1] = drive("r", "g", 2, 0)
    with pytest.raises(CompileError, match="启动相位"):
        GraphCompiler(registry).compile(source)


def test_debug_does_not_remove_driver_only_computation(registry):
    source = project()
    source.debug_mode = True
    plan = GraphCompiler(registry).compile(source)
    assert plan.ignored_nodes == []
    assert plan.execution_order == ["clock", "a", "b"]


def test_round_trip_driver_and_cadence(registry, tmp_path):
    source = project()
    source.drive_connections[0] = drive("clock", "a", 3, 0)
    loader = ProjectLoader(registry)
    path = tmp_path / "project.yaml"
    loader.save(source, path)
    loaded = loader.load(path)
    assert loaded.flow_mode == "task_sequence"
    assert loaded.drive_connections == source.drive_connections
    assert "flow_mode" not in loaded.extra
    schemas.validate(project_to_dict(loaded), schemas.load_project_schema())


def test_composite_driver_boundaries_and_instance_identity(registry):
    source = project()
    inner = Graph(nodes={k: v for k, v in source.graph.nodes.items() if k != "clock"},
                  drive_connections=[drive("a", "b")])
    source.subcomponents = [Subcomponent(id="pair", graph=inner)]
    source.graph = Graph(nodes={
        "clock": source.graph.nodes["clock"], "first": Node("first", "sub:pair"),
        "second": Node("second", "sub:pair"),
    })
    source.drive_connections = [drive("clock", "first"), drive("first", "second")]
    source.control_connections = []
    flattened = flatten_project(source)
    plan = GraphCompiler(registry).compile(flattened)
    assert plan.execution_order == ["clock", "first__a", "first__b", "second__a", "second__b"]
    assert len(plan.node_configs) == 5


def test_legacy_projects_keep_topology_and_block_end_control(registry):
    source = audio_project()
    source.flow_mode = "dataflow"
    source.drive_connections = []
    plan = GraphCompiler(registry).compile(source)
    assert plan.flow_mode == "dataflow"
    assert plan.execution_order == ["s", "r", "g"]
    assert "phase" not in plan.node_configs["g"]
    assert "phases" not in plan.tasks[0]["schedule"]


def test_timer_frequency_must_be_exact(registry):
    source = project()
    source.graph.nodes["clock"].params["frequency_hz"] = 101
    with pytest.raises(CompileError, match="不能用取整"):
        GraphCompiler(registry).compile(source)


def test_task_sequence_example(registry):
    source = ProjectLoader(registry).load(ROOT / "examples" / "task_sequence_demo.yaml")
    plan = GraphCompiler(registry).compile(flatten_project(source))
    assert plan.tasks[0]["frequency_hz"] == 100
    assert plan.execution_order == ["clock", "values", "divide", "consumer"]
    assert plan.node_configs["divide"]["params"]["factor"] == 2
    assert plan.node_configs["divide"]["params"]["phase"] == 1
    assert plan.node_configs["consumer"]["period"] == 1
    assert "frame_origin" not in plan.node_configs["consumer"]


def test_api_persists_driver_contract(registry):
    from editor_client import EditorClient as TestClient
    from orpheus_core.server.app import create_app

    name = f"task_flow_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        assert client.post("/api/projects", json={"name": name}).status_code == 201
        try:
            doc = project_to_dict(project())
            assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
            loaded = client.get(f"/api/projects/{name}").json()
            assert loaded["flow_mode"] == "task_sequence"
            assert loaded["drive_connections"] == doc["drive_connections"]
            compiled = client.post(f"/api/projects/{name}/compile")
            assert compiled.status_code == 200, compiled.text
            assert compiled.json()["flow_mode"] == "task_sequence"
            assert compiled.json()["tasks"][0]["source"] == "clock"
            run = client.post(f"/api/projects/{name}/run")
            assert run.status_code == 200, run.text
            assert run.json()["status"] == "ok", run.text
            assert run.json()["returncode"] == 0
        finally:
            assert client.delete(f"/api/projects/{name}").status_code == 200


def test_default_host_explicitly_rejects_multiple_source_tasks():
    from editor_client import EditorClient as TestClient
    from orpheus_core.server.app import create_app

    name = f"task_host_{uuid.uuid4().hex[:8]}"
    source = project()
    source.control_connections = []
    source.graph.nodes["other"] = Node("other", "orpheus.builtin.task_source",
                                      params={"frequency_hz": 200})
    source.drive_connections = [drive("clock", "a"), drive("other", "b")]
    with TestClient(create_app(ROOT)) as client:
        assert client.post("/api/projects", json={"name": name}).status_code == 201
        try:
            assert client.put(f"/api/projects/{name}", json=project_to_dict(source)).status_code == 200
            result = client.post(f"/api/projects/{name}/run")
            assert result.status_code == 400
            assert "独立 Task" in result.json()["detail"]
        finally:
            assert client.delete(f"/api/projects/{name}").status_code == 200


@pytest.mark.parametrize("kind,phase", [
    ("bulk", 0), ("bulk", 1), ("scalar", 0), ("scope", 0), ("independent", 0),
    ("sibling", 0), ("split", 0), ("history", 0),
])
def test_dynamic_generated_global_task_entries(registry, tmp_path, kind, phase):
    from orpheus_core.generator import CodeGenerator
    from test_generated_double_bank import _build

    source = project()
    if kind == "scalar":
        source = Project(flow_mode="task_sequence", block_size=32)
        source.graph.nodes = {
            "clock": Node("clock", "orpheus.builtin.wav_in",
                          params={"channels": 1, "file_path": "in.wav"}),
            "rms": Node("rms", "orpheus.builtin.probe_rms", params={"channels": 1}),
            "gain": Node("gain", "orpheus.builtin.gain",
                         params={"channels": 1, "smoothing_ms": 0.0}),
            "sink": Node("sink", "orpheus.builtin.wav_out",
                         params={"channels": 1, "file_path": "out.wav"}),
        }
        source.graph.connections = [data("clock", "rms"), data("rms", "gain"), data("gain", "sink")]
        source.drive_connections = [drive("clock", "rms"), drive("rms", "gain"), drive("gain", "sink")]
        source.control_connections = [ControlConnection(PortRef("rms", "rms"), PortRef("gain", "gain_db"))]
        with wave.open(str(tmp_path / "in.wav"), "wb") as stream:
            stream.setnchannels(1)
            stream.setsampwidth(2)
            stream.setframerate(48000)
            stream.writeframes(struct.pack("<h", 8192) * 128)
    elif kind in ("independent", "sibling", "split", "history"):
        source = Project(flow_mode="task_sequence", block_size=32)
        source.graph.nodes = {
            "clock": Node("clock", "orpheus.builtin.wav_in",
                          params={"channels": 1, "file_path": "in.wav"}),
            "batch": Node("batch", "orpheus.builtin.downrate",
                          params={"channels": 1, "factor": 2}),
            "sink": Node("sink", "orpheus.builtin.wav_out",
                         params={"channels": 1, "file_path": "out.wav"}),
        }
        source.graph.connections = [data("clock", "batch"), data("batch", "sink")]
        source.drive_connections = [drive("clock", "batch"), drive("clock", "sink", 2)]
        source.control_connections = []
        if kind == "sibling":
            source.graph.nodes["mid"] = Node("mid", "orpheus.builtin.gain", params={"channels": 1})
            source.graph.connections = [data("clock", "batch"), data("batch", "mid"), data("mid", "sink")]
            source.drive_connections = [drive("clock", "batch"), drive("batch", "mid", 2),
                                        drive("clock", "sink", 2)]
        elif kind == "split":
            source.graph.nodes["mid"] = Node("mid", "orpheus.builtin.gain", params={"channels": 1})
            source.graph.nodes["split"] = Node("split", "orpheus.builtin.block_split",
                                              params={"channels": 1, "factor": 2})
            source.graph.connections = [data("clock", "batch"), data("batch", "mid"),
                                        data("mid", "split"), data("split", "sink")]
            source.drive_connections = [drive("clock", "batch"), drive("clock", "mid", 2),
                                        drive("clock", "split"), drive("split", "sink")]
        elif kind == "history":
            del source.graph.nodes["batch"]
            source.graph.nodes["history"] = Node("history", "orpheus.builtin.circular_buffer",
                params={"channels": 1, "frame_size": 64, "hop_size": 32, "num_frames": 1})
            source.graph.nodes["mid"] = Node("mid", "orpheus.builtin.gain", params={"channels": 1})
            source.graph.connections = [data("clock", "history"), data("history", "mid"), data("mid", "sink")]
            source.drive_connections = [drive("clock", "history"), drive("history", "mid"), drive("mid", "sink")]
        with wave.open(str(tmp_path / "in.wav"), "wb") as stream:
            stream.setnchannels(1)
            stream.setsampwidth(2)
            stream.setframerate(48000)
            stream.writeframes(struct.pack("<128h", *(64 * (i + 1) for i in range(128))))
    elif kind == "scope":
        source.graph.nodes["other"] = Node("other", "orpheus.builtin.task_source",
                                          params={"frequency_hz": 200})
        source.drive_connections = [drive("clock", "a"), drive("other", "b")]
        source.control_connections = []
    else:
        source.graph.nodes["divide"] = Node("divide", "orpheus.builtin.trigger_divider",
                                           params={"factor": 2, "phase": phase})
        source.drive_connections = [drive("clock", "a"), drive("a", "divide"), drive("divide", "b")]

    plan = GraphCompiler(registry).compile(source)
    path = tmp_path / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    target_source = ROOT / "tests" / "task_flow_runner.cpp"
    (generated / "src" / "task_flow_test.c").write_bytes(target_source.read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write(
            "\nadd_executable(task_flow_test src/task_flow_test.c)\n"
            "target_compile_definitions(task_flow_test PRIVATE ORPHEUS_TEST_GENERATED"
            + (" ORPHEUS_TEST_SCOPE" if kind == "scope" else "") + ")\n"
            "target_link_libraries(task_flow_test PRIVATE orpheus_graph)\n"
        )
    _build(generated, tmp_path)
    independent_outputs = []
    for generated_mode in (False, True):
        executable = (generated / "build" / "task_flow_test.exe" if generated_mode
                      else ROOT / "build" / "tests" / "task_flow_runner.exe")
        for mode in (("task",) if kind == "scope" else ("global", "task")):
            runner_kind = "independent" if kind in ("sibling", "split", "history") else kind
            args = ([str(executable)] if generated_mode else [
                str(executable), str(path), str(ROOT / "build" / "components"),
            ]) + [mode, runner_kind, str(phase)]
            result = subprocess.run(args, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            if kind == "scalar":
                with wave.open(str(tmp_path / "out.wav"), "rb") as stream:
                    assert stream.getnframes() == 128
                    output = struct.unpack("<128h", stream.readframes(128))
                expected = 8192 * 10 ** (0.25 / 20)
                assert max(abs(value - expected) for value in output) < 2
            elif kind in ("independent", "sibling", "split", "history"):
                samples = [64 * (i + 1) for i in range(128)]
                expected = samples
                if kind == "sibling":
                    expected = [value for start in range(0,128,64) for _ in range(2)
                                for value in samples[start:start+64]]
                elif kind == "split":
                    expected = [0] * 32 + samples[:96]
                elif kind == "history":
                    expected = [0] * 32 + samples[:32]
                    for block in range(1, 4):
                        expected += samples[(block - 1) * 32:(block + 1) * 32]
                with wave.open(str(tmp_path / "out.wav"), "rb") as stream:
                    assert stream.getnframes() == len(expected)
                    output = struct.unpack(f"<{len(expected)}h", stream.readframes(len(expected)))
                assert max(abs(value - reference) for value, reference in zip(output, expected)) <= 1
                independent_outputs.append(output)
    if independent_outputs:
        assert all(output == independent_outputs[0] for output in independent_outputs)
