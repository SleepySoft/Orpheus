"""Tests for subcomponent flattening and its API integration."""

from __future__ import annotations

import shutil
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import (
    Connection, Graph, Node, PortRef, Project, SubParameter, SubPort, Subcomponent,
)
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]  # repository root


def conn(a: str, b: str) -> Connection:
    return Connection(from_ref=PortRef.parse(a), to_ref=PortRef.parse(b))


def make_sub(sub_id: str = "chain") -> Subcomponent:
    """gain -> biquad wrapped as a subcomponent with in/out ports."""
    return Subcomponent(
        id=sub_id,
        name="链路子组件",
        ports=[
            SubPort(id="in", direction="input", maps_to="gain:in"),
            SubPort(id="out", direction="output", maps_to="biquad:out"),
        ],
        graph=Graph(
            nodes={
                "gain": Node(id="gain", component="orpheus.builtin.gain",
                             params={"gain_db": -6.0, "channels": 2}),
                "biquad": Node(id="biquad", component="orpheus.builtin.biquad",
                               params={"type": "lowpass", "fc": 2000.0, "q": 0.707,
                                       "gain_db": 0.0, "channels": 2}),
            },
            connections=[conn("gain:out", "biquad:in")],
        ),
    )


def make_project() -> Project:
    project = Project(metadata={"name": "t"})
    project.graph = Graph(
        nodes={
            "src": Node(id="src", component="orpheus.builtin.signal_gen",
                        params={"frequency": 440.0, "amplitude": 0.5, "channels": 2}),
            "acc1": Node(id="acc1", component="sub:chain"),
            "sink": Node(id="sink", component="orpheus.builtin.probe_rms",
                         params={"channels": 2}),
        },
        connections=[conn("src:out", "acc1:in"), conn("acc1:out", "sink:in")],
    )
    project.subcomponents = [make_sub()]
    return project


def edge_set(graph: Graph) -> set[tuple[str, str]]:
    return {(str(c.from_ref), str(c.to_ref)) for c in graph.connections}


# ------------------------------------------------------------------ unit tests


def test_flatten_basic():
    project = make_project()
    flat = flatten_project(project)

    assert set(flat.graph.nodes) == {"src", "acc1__gain", "acc1__biquad", "sink"}
    assert edge_set(flat.graph) == {
        ("src:out", "acc1__gain:in"),
        ("acc1__gain:out", "acc1__biquad:in"),
        ("acc1__biquad:out", "sink:in"),
    }
    # params survive the expansion
    assert flat.graph.nodes["acc1__gain"].params["gain_db"] == -6.0
    # original project untouched
    assert set(project.graph.nodes) == {"src", "acc1", "sink"}
    assert len(project.subcomponents) == 1


def test_flatten_multiple_instances():
    project = make_project()
    project.graph.nodes["acc2"] = Node(id="acc2", component="sub:chain")
    project.graph.connections.append(conn("acc1:out", "acc2:in"))
    project.graph.connections.append(conn("acc2:out", "sink:in"))

    flat = flatten_project(project)
    assert {"acc1__gain", "acc1__biquad", "acc2__gain", "acc2__biquad"} <= set(flat.graph.nodes)
    assert ("acc1__biquad:out", "acc2__gain:in") in edge_set(flat.graph)
    assert ("acc2__biquad:out", "sink:in") in edge_set(flat.graph)


def test_flatten_nested_subcomponents():
    # outer: pre(gain) -> inner(sub:chain) -> post(gain); ports map to atomic nodes
    outer = Subcomponent(
        id="outer",
        ports=[SubPort(id="in", direction="input", maps_to="pre:in"),
               SubPort(id="out", direction="output", maps_to="post:out")],
        graph=Graph(
            nodes={
                "pre": Node(id="pre", component="orpheus.builtin.gain",
                            params={"gain_db": 1.0, "channels": 2}),
                "inner": Node(id="inner", component="sub:chain"),
                "post": Node(id="post", component="orpheus.builtin.gain",
                             params={"gain_db": 2.0, "channels": 2}),
            },
            connections=[conn("pre:out", "inner:in"), conn("inner:out", "post:in")],
        ),
    )
    project = make_project()
    project.graph.nodes["acc1"] = Node(id="acc1", component="sub:outer")
    project.subcomponents.append(outer)

    flat = flatten_project(project)
    assert set(flat.graph.nodes) == {
        "src", "sink", "acc1__pre", "acc1__inner__gain", "acc1__inner__biquad", "acc1__post",
    }
    edges = edge_set(flat.graph)
    assert ("src:out", "acc1__pre:in") in edges
    assert ("acc1__pre:out", "acc1__inner__gain:in") in edges
    assert ("acc1__inner__biquad:out", "acc1__post:in") in edges
    assert ("acc1__post:out", "sink:in") in edges


def test_flatten_cycle_detected():
    a = make_sub("a")
    b = make_sub("b")
    a.graph.nodes["b_inst"] = Node(id="b_inst", component="sub:b")
    b.graph.nodes["a_inst"] = Node(id="a_inst", component="sub:a")
    project = make_project()
    project.graph.nodes["acc1"] = Node(id="acc1", component="sub:a")
    project.subcomponents = [a, b]
    with pytest.raises(CompileError, match="cycle"):
        flatten_project(project)


def test_flatten_undefined_sub():
    project = make_project()
    project.subcomponents = []
    with pytest.raises(CompileError, match="undefined subcomponent"):
        flatten_project(project)


def test_flatten_invalid_maps_to():
    project = make_project()
    project.subcomponents[0].ports[0] = SubPort(id="in", direction="input", maps_to="ghost:in")
    with pytest.raises(CompileError, match="unknown node"):
        flatten_project(project)


def test_flatten_maps_to_nested_instance_rejected():
    project = make_project()
    project.subcomponents[0].graph.nodes["nested"] = Node(id="nested", component="sub:chain")
    project.subcomponents[0].ports[0] = SubPort(id="in", direction="input", maps_to="nested:in")
    with pytest.raises(CompileError, match="atomic"):
        flatten_project(project)


def test_flatten_duplicate_port_id():
    project = make_project()
    project.subcomponents[0].ports.append(SubPort(id="in", direction="input", maps_to="gain:in"))
    with pytest.raises(CompileError, match="duplicate port"):
        flatten_project(project)


def test_flatten_public_parameter_instance_override():
    project = make_project()
    project.subcomponents[0].public_parameters = [
        SubParameter(id="gain_db", direction="input", maps_to="gain:gain_db", default=-6.0),
    ]
    project.graph.nodes["acc1"].params["gain_db"] = -18.0

    flat = flatten_project(project)
    assert flat.graph.nodes["acc1__gain"].params["gain_db"] == -18.0
    assert project.subcomponents[0].graph.nodes["gain"].params["gain_db"] == -6.0


def test_flatten_rejects_public_output_override():
    project = make_project()
    project.subcomponents[0].public_parameters = [
        SubParameter(id="level", direction="output", maps_to="gain:gain_db"),
    ]
    project.graph.nodes["acc1"].params["level"] = 1.0
    with pytest.raises(CompileError, match="控制输出不可由实例赋值"):
        flatten_project(project)


def test_flatten_then_compile_with_registry():
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    plan = GraphCompiler(registry).compile(flatten_project(make_project()))
    assert set(plan.nodes) == {"src", "acc1__gain", "acc1__biquad", "sink"}
    assert plan.execution_order.index("src") < plan.execution_order.index("acc1__gain")
    assert plan.execution_order.index("acc1__biquad") < plan.execution_order.index("sink")
    assert plan.node_configs["acc1__gain"]["component"] == "orpheus.builtin.gain"


# ------------------------------------------------------------------ API tests


@pytest.fixture()
def client():
    with TestClient(create_app(ROOT)) as c:
        yield c


def _doc_with_sub() -> dict:
    return {
        "version": "0.1.0",
        "metadata": {"name": "sub_e2e"},
        "sample_rate": 48000,
        "block_size": 128,
        "subcomponents": [
            {
                "id": "chain",
                "name": "链路子组件",
                "ports": [
                    {"id": "in", "direction": "input", "maps_to": "gain:in"},
                    {"id": "out", "direction": "output", "maps_to": "biquad:out"},
                ],
                "graph": {
                    "nodes": [
                        {"id": "gain", "component": "orpheus.builtin.gain",
                         "params": {"gain_db": -6.0, "channels": 2}, "position": {"x": 0, "y": 0}},
                        {"id": "biquad", "component": "orpheus.builtin.biquad",
                         "params": {"type": "lowpass", "fc": 2000.0, "q": 0.707,
                                    "gain_db": 0.0, "channels": 2},
                         "position": {"x": 200, "y": 0}},
                    ],
                    "connections": [{"from": "gain:out", "to": "biquad:in"}],
                },
            }
        ],
        "graph": {
            "nodes": [
                {"id": "wav_in", "component": "orpheus.builtin.wav_in",
                 "params": {"file_path": "test_input.wav", "channels": 2},
                 "position": {"x": 0, "y": 0}},
                {"id": "acc1", "component": "sub:chain", "params": {},
                 "position": {"x": 200, "y": 0}},
                {"id": "wav_out", "component": "orpheus.builtin.wav_out",
                 "params": {"file_path": "outputs/test_output.wav", "channels": 2,
                            "sample_rate": 48000},
                 "position": {"x": 400, "y": 0}},
            ],
            "connections": [
                {"from": "wav_in:out", "to": "acc1:in"},
                {"from": "acc1:out", "to": "wav_out:in"},
            ],
        },
    }


def test_api_compile_with_subcomponent(client):
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        assert client.post("/api/projects", json={"name": name}).status_code == 201
        resp = client.put(f"/api/projects/{name}", json=_doc_with_sub())
        assert resp.status_code == 200, resp.text

        # document round-trips subcomponents
        doc = client.get(f"/api/projects/{name}").json()
        assert doc["subcomponents"][0]["id"] == "chain"
        assert len(doc["subcomponents"][0]["graph"]["nodes"]) == 2

        resp = client.post(f"/api/projects/{name}/compile")
        assert resp.status_code == 200, resp.text
        assert resp.json()["nodes"] == 4  # flattened: wav_in, acc1__gain, acc1__biquad, wav_out
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_api_run_with_subcomponent(client):
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        assert client.post("/api/projects", json={"name": name}).status_code == 201
        # input wav must live inside the project dir (paths are project-relative)
        shutil.copy2(ROOT / "examples" / "test_input.wav", ROOT / "workspace" / name / "test_input.wav")
        resp = client.put(f"/api/projects/{name}", json=_doc_with_sub())
        assert resp.status_code == 200, resp.text

        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        assert "outputs/test_output.wav" in result["outputs"]
    finally:
        client.delete(f"/api/projects/{name}")
