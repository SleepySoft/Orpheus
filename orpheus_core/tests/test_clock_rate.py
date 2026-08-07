"""Tests for clock-domain validation and rate-divisor scheduling."""

from __future__ import annotations

import uuid
import wave
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


def sig(nid: str, channels: int = 1) -> Node:
    return Node(id=nid, component="orpheus.builtin.signal_gen",
                params={"frequency": 440.0, "amplitude": 0.5, "channels": channels})


def make_project(nodes, connections) -> Project:
    project = Project(metadata={"name": "t"})
    project.graph = Graph(nodes={n.id: n for n in nodes}, connections=connections)
    return project


# ------------------------------------------------------------------ clock domains


def test_undriven_flow_rejected(compiler):
    """A floating flow inside a clocked graph cannot start -> compile error."""
    project = make_project(
        [
            Node(id="din", component="orpheus.builtin.device_in", params={"channels": 2}),
            Node(id="dout", component="orpheus.builtin.device_out", params={"channels": 2}),
            Node(id="flt", component="orpheus.builtin.gain",
                 params={"gain_db": 0.0, "channels": 1}),  # 悬浮流，无时钟源
            Node(id="m", component="orpheus.builtin.probe_rms", params={"channels": 1}),
        ],
        [conn("din:out", "dout:in"), conn("flt:out", "m:in")],
    )
    with pytest.raises(CompileError, match="not driven by any clock"):
        compiler.compile(project)


def test_device_flow_accepted(compiler):
    project = make_project(
        [
            Node(id="din", component="orpheus.builtin.device_in", params={"channels": 2}),
            Node(id="g", component="orpheus.builtin.gain", params={"gain_db": 0.0, "channels": 2}),
            Node(id="dout", component="orpheus.builtin.device_out", params={"channels": 2}),
        ],
        [conn("din:out", "g:in"), conn("g:out", "dout:in")],
    )
    plan = compiler.compile(project)
    assert len(plan.nodes) == 3


def test_clock_free_graph_runs_on_implicit_clock(compiler):
    """No clock sources at all -> implicit host clock (legacy behavior)."""
    project = make_project(
        [sig("sig"), Node(id="m", component="orpheus.builtin.probe_rms", params={"channels": 1})],
        [conn("sig:out", "m:in")],
    )
    plan = compiler.compile(project)
    assert len(plan.nodes) == 2


# ------------------------------------------------------------------ rate scheduling


def test_resample_divisor_propagation(compiler):
    project = make_project(
        [
            sig("sig"),
            Node(id="rs", component="orpheus.builtin.resample",
                 params={"factor": 2, "channels": 1}),
            Node(id="out", component="orpheus.builtin.wav_out",
                 params={"file_path": "o.wav", "channels": 1, "sample_rate": 24000}),
        ],
        [conn("sig:out", "rs:in"), conn("rs:out", "out:in")],
    )
    plan = compiler.compile(project)
    assert plan.node_configs["sig"]["divisor"] == 1
    assert plan.node_configs["rs"]["divisor"] == 1  # runs every block at input rate
    assert plan.node_configs["out"]["divisor"] == 2  # downstream fires every 2 blocks
    assert plan.node_configs["out"]["sample_rate"] == 24000


def test_downrate_superblock(compiler):
    project = make_project(
        [
            sig("sig"),
            Node(id="dr", component="orpheus.builtin.downrate",
                 params={"factor": 4, "channels": 1}),
            Node(id="m", component="orpheus.builtin.probe_rms", params={"channels": 1}),
        ],
        [conn("sig:out", "dr:in"), conn("dr:out", "m:in")],
    )
    plan = compiler.compile(project)
    assert plan.node_configs["m"]["divisor"] == 4
    assert plan.node_configs["m"]["frames"] == 128 * 4  # superblock quantum
    assert plan.node_configs["m"]["sample_rate"] == 48000  # rate unchanged


def test_divisor_merge_mismatch_rejected(compiler):
    project = make_project(
        [
            sig("s0"),
            sig("s1"),
            Node(id="dr", component="orpheus.builtin.downrate",
                 params={"factor": 4, "channels": 1}),
            Node(id="mix", component="orpheus.builtin.mixer",
                 params={"gain0": 1.0, "gain1": 1.0, "channels": 1}),
        ],
        [conn("s0:out", "dr:in"), conn("dr:out", "mix:in0"), conn("s1:out", "mix:in1")],
    )
    with pytest.raises(CompileError, match="rate mismatch"):
        compiler.compile(project)


# ------------------------------------------------------------------ e2e


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components" / "liborpheus_builtin_resample.dll").exists(),
    reason="runtime and components not built",
)
def test_resample_offline_run_and_generated_match():
    """wav_in(48k) -> resample(2) -> wav_out(24k): dynamic vs generated identical."""
    import shutil

    from fastapi.testclient import TestClient

    from orpheus_core.server.app import create_app

    name = f"test_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            pdir = ROOT / "workspace" / name
            shutil.copy2(ROOT / "examples" / "test_input.wav", pdir / "test_input.wav")
            doc = client.get(f"/api/projects/{name}").json()
            doc["graph"] = {
                "nodes": [
                    {"id": "wav_in", "component": "orpheus.builtin.wav_in",
                     "params": {"file_path": "test_input.wav", "channels": 2},
                     "position": {"x": 0, "y": 0}},
                    {"id": "rs", "component": "orpheus.builtin.resample",
                     "params": {"factor": 2, "channels": 2},
                     "position": {"x": 200, "y": 0}},
                    {"id": "wav_out", "component": "orpheus.builtin.wav_out",
                     "params": {"file_path": "outputs/out.wav", "channels": 2,
                                "sample_rate": 24000},
                     "position": {"x": 400, "y": 0}},
                ],
                "connections": [
                    {"from": "wav_in:out", "to": "rs:in"},
                    {"from": "rs:out", "to": "wav_out:in"},
                ],
            }
            assert client.put(f"/api/projects/{name}", json=doc).status_code == 200

            resp = client.post(f"/api/projects/{name}/run")
            assert resp.json()["status"] == "ok", resp.json()
            out_wav = pdir / "outputs" / "out.wav"
            with wave.open(str(out_wav), "rb") as w:
                assert w.getframerate() == 24000
                # 48000 input frames 2:1 decimated; an incomplete trailing output
                # block is dropped in block-based processing (<= 128 frames)
                assert abs(w.getnframes() - 24000) <= 128
            dynamic_bytes = out_wav.read_bytes()

            resp = client.post(f"/api/projects/{name}/run_generated")
            result = resp.json()
            assert result["status"] == "ok", result["stderr"]
            assert out_wav.read_bytes() == dynamic_bytes
        finally:
            client.delete(f"/api/projects/{name}")


def test_generator_sanitized_node_id():
    """节点 id 必须清洗为合法 C 标识符（子组件展开/用户命名可能含 '.'、'-'、空格）。"""
    from orpheus_core.generator import CodeGenerator
    from orpheus_core.registry import Registry

    gen = CodeGenerator(Registry(), ROOT)
    assert gen._sanitized_node_id("fx__g") == "fx__g"
    assert gen._sanitized_node_id("my.gain") == "my_gain"
    assert gen._sanitized_node_id("a-b c") == "a_b_c"


def test_generator_sample_rate_adopts_graph_rate(compiler):
    """发生器的 sample_rate 参数成为图采样率（时钟以它为准）。"""
    from orpheus_core.project import Graph, Node, Project

    project = Project(metadata={"name": "t"})
    project.graph = Graph(
        nodes={
            "sig": Node(id="sig", component="orpheus.builtin.signal_gen",
                        params={"sample_rate": 8000, "frequency": 440.0,
                                "amplitude": 0.5, "channels": 1}),
        },
        connections=[],
    )
    plan = compiler.compile(project)
    assert plan.sample_rate == 8000


def test_generators_sample_rate_conflict(compiler):
    """多个时钟源的 sample_rate 不一致必须报错。"""
    from orpheus_core.compiler import CompileError
    from orpheus_core.project import Graph, Node, Project

    project = Project(metadata={"name": "t"})
    project.graph = Graph(
        nodes={
            "sig1": Node(id="sig1", component="orpheus.builtin.signal_gen",
                         params={"sample_rate": 8000, "channels": 1}),
            "sig2": Node(id="sig2", component="orpheus.builtin.signal_gen",
                         params={"sample_rate": 16000, "channels": 1}),
        },
        connections=[],
    )
    with pytest.raises(CompileError, match="disagree"):
        compiler.compile(project)


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components" / "liborpheus_builtin_gain.dll").exists(),
    reason="runtime and components not built",
)
def test_gain_float_param_dynamic_generated_match():
    """wav_in -> gain(gain_db=-6.0 float) -> wav_out: dynamic vs generated identical.

    回归：生成路径必须按 manifest 参数类型下发 float 参数（不能把 "-6.0" 当 STRING），
    否则生成工程内 gain 以 0dB 运行，与动态路径输出不一致。
    """
    import shutil

    from fastapi.testclient import TestClient

    from orpheus_core.server.app import create_app

    name = f"test_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            pdir = ROOT / "workspace" / name
            shutil.copy2(ROOT / "examples" / "test_input.wav", pdir / "test_input.wav")
            doc = client.get(f"/api/projects/{name}").json()
            doc["graph"] = {
                "nodes": [
                    {"id": "wav_in", "component": "orpheus.builtin.wav_in",
                     "params": {"file_path": "test_input.wav", "channels": 2},
                     "position": {"x": 0, "y": 0}},
                    {"id": "gain1", "component": "orpheus.builtin.gain",
                     "params": {"gain_db": "-6.0", "channels": 2},
                     "position": {"x": 200, "y": 0}},
                    {"id": "wav_out", "component": "orpheus.builtin.wav_out",
                     "params": {"file_path": "outputs/out.wav", "channels": 2,
                                "sample_rate": 48000},
                     "position": {"x": 400, "y": 0}},
                ],
                "connections": [
                    {"from": "wav_in:out", "to": "gain1:in"},
                    {"from": "gain1:out", "to": "wav_out:in"},
                ],
            }
            assert client.put(f"/api/projects/{name}", json=doc).status_code == 200

            resp = client.post(f"/api/projects/{name}/run")
            assert resp.json()["status"] == "ok", resp.json()
            dynamic_bytes = (pdir / "outputs" / "out.wav").read_bytes()

            resp = client.post(f"/api/projects/{name}/run_generated")
            result = resp.json()
            assert result["status"] == "ok", result["stderr"]
            assert (pdir / "outputs" / "out.wav").read_bytes() == dynamic_bytes
        finally:
            client.delete(f"/api/projects/{name}")
