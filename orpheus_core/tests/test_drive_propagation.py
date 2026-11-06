"""声明式驱动传播：入口、解耦边界与双执行路径。"""

import copy
from dataclasses import asdict
import json
from pathlib import Path
import subprocess
import wave

import jsonschema
import pytest

from orpheus_core import schemas
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import (
    Connection, ControlConnection, DriveConnection, Graph, Node, PortRef,
    Project, ProjectLoader, Subcomponent, SubPort, project_to_dict,
)
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project
from test_generated_double_bank import _build

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def registry():
    result = Registry([ROOT / "components"])
    result.scan()
    return result


def wire(source, target):
    return Connection(PortRef(source, "out"), PortRef(target, "in"))


def audio_chain():
    return Project(flow_mode="task_sequence", block_size=32, graph=Graph(
        nodes={
            "source": Node("source", "orpheus.builtin.signal_gen", params={"channels": 1}),
            "gain": Node("gain", "orpheus.builtin.gain", params={"channels": 1}),
            "sink": Node("sink", "orpheus.builtin.gain", params={"channels": 1}),
        },
        connections=[wire("source", "gain"), wire("gain", "sink")],
    ))


def test_audio_chain_inherits_drive_without_manual_call_edges(registry):
    source = audio_chain()
    plan = GraphCompiler(registry).compile(source)
    assert plan.call_graph == {
        "source": [{"node": "gain", "every": 1, "phase": 0}],
        "gain": [{"node": "sink", "every": 1, "phase": 0}],
        "sink": [],
    }
    assert source.drive_connections == []
    assert project_to_dict(source).get("drive_connections") is None


def test_explicit_entry_wins_without_duplicate_inherited_calls(registry):
    source = audio_chain()
    source.drive_connections = [
        DriveConnection(PortRef("source", "out"), PortRef("sink", "in"), 2, 1),
    ]
    plan = GraphCompiler(registry).compile(source)
    assert plan.call_graph["gain"] == []
    assert plan.call_graph["source"] == [
        {"node": "gain", "every": 1, "phase": 0},
        {"node": "sink", "every": 2, "phase": 1},
    ]
    assert (plan.node_configs["sink"]["period"], plan.node_configs["sink"]["phase"]) == (2, 1)


def test_declared_output_boundary_requires_an_independent_entry(registry):
    local = registry.fork()
    info = copy.deepcopy(local.get("orpheus.builtin.gain"))
    info.manifest["ports"][1]["propagate_drive"] = False
    local._components[info.id] = info
    with pytest.raises(CompileError, match="没有驱动来源"):
        GraphCompiler(local).compile(audio_chain())
    source = audio_chain()
    source.drive_connections = [
        DriveConnection(PortRef("source", "out"), PortRef("sink", "in")),
    ]
    plan = GraphCompiler(local).compile(source)
    assert plan.call_graph["gain"] == []


def test_control_connections_never_create_calls(registry):
    source = Project(flow_mode="task_sequence", graph=Graph(nodes={
        "clock": Node("clock", "orpheus.builtin.task_source"),
        "a": Node("a", "orpheus.builtin.bulk_relay"),
        "b": Node("b", "orpheus.builtin.bulk_relay"),
    }), drive_connections=[DriveConnection(PortRef("clock", "out"), PortRef("a", "in"))],
        control_connections=[ControlConnection(PortRef("a", "values"), PortRef("b", "values"))])
    with pytest.raises(CompileError, match="没有驱动来源"):
        GraphCompiler(registry).compile(source)


@pytest.mark.parametrize("value", ["false", 0, None, {}])
def test_manifest_rejects_non_boolean_propagation(registry, value):
    manifest = copy.deepcopy(registry.get("orpheus.builtin.gain").manifest)
    manifest["ports"][1]["propagate_drive"] = value
    with pytest.raises(jsonschema.ValidationError):
        schemas.validate(manifest, schemas.load_component_manifest_schema())


def test_composite_entry_and_boundary_survive_flattening(registry):
    source = audio_chain()
    inner = Graph(nodes={"a": Node("a", "orpheus.builtin.gain", params={"channels": 1}),
                         "b": Node("b", "orpheus.builtin.gain", params={"channels": 1})},
                  connections=[wire("a", "b")])
    source.subcomponents = [Subcomponent(
        id="pair", graph=inner,
        ports=[SubPort("in", "input", "a:in"), SubPort("out", "output", "b:out", False)],
    )]
    source.graph.nodes["gain"].component = "sub:pair"
    source.drive_connections = [
        DriveConnection(PortRef("source", "out"), PortRef("gain", "in")),
        DriveConnection(PortRef("source", "out"), PortRef("sink", "in")),
    ]
    flat = flatten_project(source, registry)
    assert ("gain__b:out", "sink:in") in flat.graph.drive_barriers
    plan = GraphCompiler(registry).compile(flat)
    assert plan.call_graph["gain__a"] == [{"node": "gain__b", "every": 1, "phase": 0}]
    assert plan.call_graph["gain__b"] == []
    document = project_to_dict(source)
    assert document["subcomponents"][0]["ports"][1]["propagate_drive"] is False
    schemas.validate(document, schemas.load_project_schema())


def test_async_buffer_domains_are_separate_and_read_chain_propagates(registry):
    source = ProjectLoader(registry).load(ROOT / "examples/trigger_async_buffer_demo.yaml")
    plan = GraphCompiler(registry).compile(flatten_project(source))
    assert plan.call_graph["writer"] == []
    assert plan.call_graph["consumer"] == [{"node": "buffer", "every": 1, "phase": 0}]
    assert plan.call_graph["buffer"] == [{"node": "reader", "every": 1, "phase": 0}]
    assert plan.node_configs["writer"]["task"] == "task_producer"
    assert plan.node_configs["buffer"]["task"] == "task_consumer"
    edge = next(buffer for buffer in plan.buffers.values() if buffer.get("task_bridge"))
    assert (edge["producer_frames"], edge["frame_count"], edge["capacity_frames"]) == (32, 48, 192)
    source.drive_connections.clear()
    with pytest.raises(CompileError, match="没有驱动来源"):
        GraphCompiler(registry).compile(source)

def test_composite_entry_does_not_reseed_nodes_after_an_internal_boundary(registry):
    local = registry.fork()
    info = copy.deepcopy(local.get("orpheus.builtin.gain"))
    info.manifest["ports"][1]["propagate_drive"] = False
    local._components[info.id] = info
    source = audio_chain()
    source.graph.nodes.pop("sink")
    source.graph.connections = [wire("source", "gain")]
    source.graph.nodes["gain"].component = "sub:pair"
    source.subcomponents = [Subcomponent(
        id="pair",
        ports=[SubPort("in", "input", "a:in")],
        graph=Graph(nodes={
            "a": Node("a", "orpheus.builtin.gain", params={"channels": 1}),
            "b": Node("b", "orpheus.builtin.gain", params={"channels": 1}),
        }, connections=[wire("a", "b")]),
    )]
    source.drive_connections = [
        DriveConnection(PortRef("source", "out"), PortRef("gain", "in")),
    ]
    with pytest.raises(CompileError, match="gain__b.*没有驱动来源"):
        GraphCompiler(local).compile(flatten_project(source, local))


def test_debug_prunes_undriven_nodes_beyond_declared_boundary(registry):
    source = audio_chain()
    source.graph.nodes["gain"].component = "orpheus.builtin.downrate"
    source.graph.nodes["gain"].params = {"channels": 1, "factor": 2}
    source.debug_mode = True
    plan = GraphCompiler(registry).compile(source)
    assert plan.ignored_nodes == ["sink"]
    assert plan.call_graph["gain"] == []


def test_async_buffer_never_pumps_consumer_and_matches_generated(registry, tmp_path):
    source = ProjectLoader(registry).load(ROOT / "examples/trigger_async_buffer_demo.yaml")
    source.graph.nodes["output"].params["file_path"] = "out.wav"
    plan = GraphCompiler(registry).compile(flatten_project(source))
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    components = {cfg["component"].replace(".", "_") for cfg in plan.node_configs.values()}
    build = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                            "orpheus_runtime", *sorted(components)],
                           capture_output=True, text=True, encoding="utf-8")
    assert build.returncode == 0, build.stdout + build.stderr
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    generated_exe = _build(generated, tmp_path)
    runs = [
        ([str(ROOT / "build/orpheus_runtime.exe"), str(plan_path), str(ROOT / "build/components")], tmp_path),
        ([str(generated_exe)], generated),
    ]
    outputs = []
    for command, directory in runs:
        for arguments, expected_frames in (
            (["--task", "task_producer", "4"], 0),
            (["--task", "task_producer", "3", "--task", "task_consumer", "2"] * 12, 1152),
        ):
            result = subprocess.run([*command, *arguments], cwd=directory,
                                    capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            with wave.open(str(directory / "out.wav"), "rb") as output:
                assert output.getnframes() == expected_frames
                assert output.getnchannels() == 2
                if expected_frames:
                    assert any(output.readframes(expected_frames))
        outputs.append((directory / "out.wav").read_bytes())
    assert outputs[0] == outputs[1]
