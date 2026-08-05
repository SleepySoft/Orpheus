"""Tests for variable-count ports (interleave/deinterleave) in the compiler."""

from __future__ import annotations

from pathlib import Path

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def compiler():
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    return GraphCompiler(registry)


def conn(a: str, b: str) -> Connection:
    return Connection(from_ref=PortRef.parse(a), to_ref=PortRef.parse(b))


def make_project(nodes: dict[str, Node], connections: list[Connection]) -> Project:
    project = Project(metadata={"name": "t"})
    project.graph = Graph(nodes=nodes, connections=connections)
    return project


def test_deinterleave_expands_output_pins(compiler):
    project = make_project(
        {
            "src": Node(id="src", component="orpheus.builtin.signal_gen",
                        params={"frequency": 440.0, "amplitude": 0.5, "channels": 3}),
            "de": Node(id="de", component="orpheus.builtin.deinterleave",
                       params={"channels": 3}),
            "m0": Node(id="m0", component="orpheus.builtin.probe_rms", params={"channels": 1}),
            "m2": Node(id="m2", component="orpheus.builtin.probe_rms", params={"channels": 1}),
        },
        [
            conn("src:out", "de:in"),
            conn("de:out0", "m0:in"),
            conn("de:out2", "m2:in"),  # out1 intentionally unconnected
        ],
    )
    plan = compiler.compile(project)
    cfg = plan.node_configs["de"]
    assert cfg["output_ports"] == ["out0", "out1", "out2"]
    assert cfg["input_ports"] == ["in"]
    # buffers on deinterleave outputs are mono
    mono = [b for b in plan.buffers.values() if b["from"].startswith("de:")]
    assert len(mono) == 2
    assert all(b["channels"] == 1 for b in mono)
    # input buffer carries 3 channels
    src_buf = [b for b in plan.buffers.values() if b["from"] == "src:out"][0]
    assert src_buf["channels"] == 3


def test_interleave_expands_input_pins(compiler):
    project = make_project(
        {
            "s0": Node(id="s0", component="orpheus.builtin.signal_gen",
                       params={"frequency": 440.0, "amplitude": 0.5, "channels": 1}),
            "il": Node(id="il", component="orpheus.builtin.interleave",
                       params={"channels": 4}),
            "out": Node(id="out", component="orpheus.builtin.probe_rms",
                        params={"channels": 4}),
        },
        [conn("s0:out", "il:in2"), conn("il:out", "out:in")],  # in0/in1/in3 unconnected
    )
    plan = compiler.compile(project)
    cfg = plan.node_configs["il"]
    assert cfg["input_ports"] == ["in0", "in1", "in2", "in3"]
    assert cfg["output_ports"] == ["out"]
    il_buf = [b for b in plan.buffers.values() if b["from"] == "il:out"][0]
    assert il_buf["channels"] == 4


def test_variable_pin_channel_mismatch_detected(compiler):
    project = make_project(
        {
            "src": Node(id="src", component="orpheus.builtin.signal_gen",
                        params={"frequency": 440.0, "amplitude": 0.5, "channels": 2}),
            "de": Node(id="de", component="orpheus.builtin.deinterleave",
                       params={"channels": 2}),
            "g": Node(id="g", component="orpheus.builtin.gain",
                      params={"gain_db": 0.0, "channels": 2}),  # expects 2ch, gets mono
        },
        [conn("src:out", "de:in"), conn("de:out0", "g:in")],
    )
    with pytest.raises(CompileError, match="channels mismatch"):
        compiler.compile(project)


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components" / "liborpheus_builtin_interleave.dll").exists(),
    reason="runtime and components not built",
)
def test_api_run_channel_map_example():
    """wav_in -> deinterleave -> swap L/R + gain -> interleave -> wav_out, end to end."""
    import shutil
    import uuid

    from fastapi.testclient import TestClient

    from orpheus_core.server.app import create_app

    name = f"test_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            resp = client.post("/api/projects", json={"name": name, "from_example": "wav_channel_map"})
            assert resp.status_code == 201, resp.text
            resp = client.post(f"/api/projects/{name}/run")
            assert resp.status_code == 200, resp.text
            result = resp.json()
            assert result["status"] == "ok", result["stderr"]
            assert "outputs/test_output.wav" in result["outputs"]
        finally:
            client.delete(f"/api/projects/{name}")


def test_input_pin_driven_twice_rejected(compiler):
    project = make_project(
        {
            "s0": Node(id="s0", component="orpheus.builtin.signal_gen",
                       params={"frequency": 440.0, "amplitude": 0.5, "channels": 1}),
            "s1": Node(id="s1", component="orpheus.builtin.signal_gen",
                       params={"frequency": 880.0, "amplitude": 0.5, "channels": 1}),
            "m": Node(id="m", component="orpheus.builtin.probe_rms", params={"channels": 1}),
        },
        [conn("s0:out", "m:in"), conn("s1:out", "m:in")],
    )
    with pytest.raises(CompileError, match="multiple sources"):
        compiler.compile(project)


def test_unknown_variable_pin_rejected(compiler):
    project = make_project(
        {
            "src": Node(id="src", component="orpheus.builtin.signal_gen",
                        params={"frequency": 440.0, "amplitude": 0.5, "channels": 2}),
            "de": Node(id="de", component="orpheus.builtin.deinterleave",
                       params={"channels": 2}),
            "m": Node(id="m", component="orpheus.builtin.probe_rms", params={"channels": 1}),
        },
        [conn("src:out", "de:in"), conn("de:out5", "m:in")],  # only out0/out1 exist
    )
    with pytest.raises(CompileError, match="unknown source port"):
        compiler.compile(project)
