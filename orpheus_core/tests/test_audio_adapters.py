"""通用SRC、拆块和重叠窗口的签名、调度及动态/生成一致性。"""
from __future__ import annotations

import math
import json
import struct
import subprocess
import uuid
import wave
from dataclasses import asdict
from pathlib import Path

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def compiler():
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    return GraphCompiler(registry)


def project_for(kind: str) -> Project:
    project = Project(metadata={"name": "通用音频适配"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    nodes = [Node(id="source", component="orpheus.builtin.signal_gen",
                  params={"channels": 2, "sample_rate": 48000})]
    if kind == "src":
        specs = [
            ("batch", "downrate", {"factor": 13}),
            ("down", "rational_resample", {"up": 3, "down": 13}),
            ("low", "gain", {}),
            ("up", "rational_resample", {"up": 13, "down": 3}),
            ("split", "block_split", {"factor": 13}),
        ]
    else:
        specs = [("batch", "downrate", {"factor": 4})]
        if kind == "split":
            specs.append(("split", "block_split", {"factor": 4}))
        else:
            specs.append(("window", "circular_buffer",
                          {"frame_size": 256, "hop_size": 128, "num_frames": 1}))
    specs.append(("consumer", "gain", {}))
    for node_id, component, params in specs:
        nodes.append(Node(id=node_id, component=f"orpheus.builtin.{component}",
                          params={"channels": 2, **params}))
    project.graph = Graph(nodes={node.id: node for node in nodes}, connections=[
        Connection(from_ref=PortRef(left.id, "out"), to_ref=PortRef(right.id, "in"))
        for left, right in zip(nodes, nodes[1:])
    ])
    return project


def test_rational_src_exact_rate_and_clock(compiler):
    plan = compiler.compile(project_for("src"))
    assert plan.schedule["tick"] == 32
    assert plan.node_configs["low"]["sample_rate_ratio"] == [144000, 13]
    assert plan.node_configs["low"]["sample_rate"] == pytest.approx(144000 / 13)
    assert plan.node_configs["low"]["frames"] == 96
    assert plan.schedule["periods"] == {
        "source": 1, "batch": 1, "down": 13, "low": 13,
        "up": 13, "split": 1, "consumer": 1,
    }
    assert plan.node_configs["split"]["block_size"] == 416
    assert plan.node_configs["split"]["frames"] == 32
    assert plan.node_configs["consumer"]["sample_rate"] == 48000


def test_integer_decimation_dynamic_generated_global_task(compiler, tmp_path):
    from test_generated_double_bank import _build

    frames = 4096
    samples = [[((frame * 101 + channel * 719) % 16001) - 8000 for channel in range(10)]
               for frame in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(10)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack("<10h", *row) for row in samples))
    cases = [(16, 0, 10, 1), (64, 0, 1, 1), (16, 15, 2, 1),
             (64, 63, 2, 1), (2, 0, 2, 0)]
    nodes = [Node(id="source", component="orpheus.builtin.wav_in",
                  params={"channels": 10, "file_path": "input.wav"})]
    edges = []
    for i, (factor, phase, channels, mode) in enumerate(cases):
        nodes.extend([
            Node(id=f"route{i}", component="orpheus.builtin.channel_router",
                 params={"channels_in": 10, "channels_out": channels,
                         "indices": ",".join(str(c) for c in range(channels))}),
            Node(id=f"down{i}", component="orpheus.builtin.resample",
                 params={"channels": channels, "factor": factor,
                         **({"mode": mode, "phase": phase} if mode else {})}),
            Node(id=f"output{i}", component="orpheus.builtin.wav_out",
                 params={"channels": channels, "file_path": f"output{i}.wav"}),
        ])
        for left, right in (("source", f"route{i}"), (f"route{i}", f"down{i}"),
                            (f"down{i}", f"output{i}")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
    project = Project(metadata={"name": "整数抽取双路径"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    project.graph = Graph(nodes={n.id: n for n in reversed(nodes)}, connections=edges)
    plan = compiler.compile(project)
    assert not plan.warnings
    for i, (factor, _, _, _) in enumerate(cases):
        assert plan.node_configs[f"output{i}"]["sample_rate"] == 48000 / factor
        assert plan.schedule["periods"][f"output{i}"] == factor
    path = tmp_path / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(compiler.registry, ROOT).generate(plan, generated)
    executable = _build(generated, tmp_path)
    runtime = ROOT / "build/orpheus_runtime.exe"
    components = ROOT / "build/components"
    commands = [
        [str(runtime), str(path), str(components), "--run", str(frames // 32)],
        [str(executable), str(frames // 32)],
        [str(runtime), str(path), str(components), "--task", "default", str(frames // 32)],
        [str(executable), "0", "--task", "default", str(frames // 32)],
    ]
    references = {}
    for command in commands:
        for i in range(len(cases)):
            (tmp_path / f"output{i}.wav").unlink(missing_ok=True)
        result = subprocess.run(command, cwd=tmp_path, capture_output=True,
                                text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        for i, (factor, phase, channels, mode) in enumerate(cases):
            output = tmp_path / f"output{i}.wav"
            data = output.read_bytes()
            assert data == references.setdefault(i, data)
            with wave.open(str(output), "rb") as stream:
                assert (stream.getnframes(), stream.getframerate(), stream.getnchannels()) == (
                    frames // factor, 48000 // factor, channels,
                )
                actual = struct.unpack(f"<{frames // factor * channels}h",
                                       stream.readframes(frames // factor))
            expected = []
            for frame in range(0, frames, factor):
                for channel in range(channels):
                    value = samples[frame + phase][channel] if mode else sum(
                        samples[j][channel] for j in range(frame, frame + factor)
                    ) / factor
                    expected.append(int(value * 32767 / 32768))
            assert max(abs(a - b) for a, b in zip(actual, expected)) <= 1


def test_split_restores_small_quantum_without_resampling(compiler):
    plan = compiler.compile(project_for("split"))
    assert plan.node_configs["split"]["block_size"] == 128
    assert plan.node_configs["split"]["frames"] == 32
    assert plan.node_configs["split"]["sample_rate"] == 48000
    assert plan.schedule["periods"]["consumer"] == 1


def test_lowrate_fdp_can_use_three_128_frame_kernels_per_batch(compiler):
    project = project_for("src")
    project.graph.nodes["low_batch"] = Node(
        id="low_batch", component="orpheus.builtin.downrate",
        params={"channels": 2, "factor": 4},
    )
    project.graph.nodes["kernel"] = Node(
        id="kernel", component="orpheus.builtin.placeholder", params={
            "interface": '[{"id":"in","direction":"input","channels":2},'
                         '{"id":"out","direction":"output","channels":8}]',
            "mode": "silence", "routes": "-1",
        },
    )
    project.graph.nodes["low_split"] = Node(
        id="low_split", component="orpheus.builtin.block_split",
        params={"channels": 8, "factor": 4},
    )
    for key in ("up", "split", "consumer"):
        project.graph.nodes[key].params["channels"] = 8
    project.graph.connections = [
        edge for edge in project.graph.connections if str(edge.from_ref) != "low:out"
    ]
    for left, right in (("low", "low_batch"), ("low_batch", "kernel"),
                        ("kernel", "low_split"), ("low_split", "up")):
        project.graph.connections.append(
            Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in"))
        )
    plan = compiler.compile(project)
    assert plan.node_configs["kernel"]["frames"] == 384
    assert plan.node_configs["kernel"]["sample_rate_ratio"] == [144000, 13]
    assert plan.schedule["periods"]["kernel"] == 52
    assert plan.node_configs["low_split"]["frames"] == 96
    assert plan.schedule["periods"]["low_split"] == 13
    assert plan.node_configs["consumer"]["frames"] == 32


def test_rational_roundtrip_does_not_depend_on_node_declaration_order(compiler):
    project = project_for("src")
    project.graph.nodes["batch"].params["factor"] = 11
    project.graph.nodes["down"].params.update(up=7, down=11)
    project.graph.nodes["up"].params.update(up=11, down=7)
    project.graph.nodes["split"].params["factor"] = 11
    project.graph.nodes["consumer"] = Node(
        id="consumer", component="orpheus.builtin.wav_out",
        params={"channels": 2, "file_path": "out.wav"},
    )
    project.graph.nodes = dict(reversed(list(project.graph.nodes.items())))
    plan = compiler.compile(project)
    assert plan.node_configs["low"]["sample_rate_ratio"] == [336000, 11]
    assert plan.node_configs["consumer"]["sample_rate"] == 48000


def test_window_has_full_output_capacity_and_preserves_hop(compiler, tmp_path):
    plan = compiler.compile(project_for("window"))
    assert plan.node_configs["window"]["output_port_block_sizes"] == {"out": 256}
    assert plan.node_configs["consumer"]["frames"] == 256
    assert plan.node_configs["consumer"]["frame_step"] == 128
    assert plan.schedule["periods"]["window"] == 4
    assert plan.schedule["periods"]["consumer"] == 4
    assert next(buf for buf in plan.buffers.values()
                if buf["from"] == "window:out")["frame_count"] == 256
    CodeGenerator(compiler.registry, ROOT).generate(plan, tmp_path / "generated")
    source = (tmp_path / "generated" / "src" / "orpheus_graph.c").read_text(encoding="utf-8")
    assert "* (uint64_t)128;" in source
    assert "(node->calls - 1u) * (uint64_t)128;" in source


@pytest.mark.parametrize("kind", ["src", "split", "window"])
def test_invalid_adapter_batches_fail_at_compile_time(compiler, kind):
    project = project_for(kind)
    if kind == "src":
        project.graph.nodes["batch"].params["factor"] = 1
    elif kind == "split":
        project.graph.nodes["split"].params["factor"] = 3
    else:
        project.graph.nodes["window"].params["hop_size"] = 64
    with pytest.raises(CompileError, match="块长"):
        compiler.compile(project)


def test_fractional_rate_cannot_be_silently_written_as_integer_wav(compiler):
    project = project_for("src")
    project.graph.nodes = {key: value for key, value in project.graph.nodes.items()
                           if key in {"source", "batch", "down", "low"}}
    project.graph.connections = project.graph.connections[:3]
    project.graph.nodes["low"] = Node(
        id="low", component="orpheus.builtin.wav_out",
        params={"channels": 2, "file_path": "out.wav"},
    )
    with pytest.raises(CompileError, match="WAV只支持整数采样率"):
        compiler.compile(project)


def test_fractional_rate_cannot_be_silently_truncated_by_physical_io(compiler):
    project = project_for("src")
    project.graph.nodes = {key: value for key, value in project.graph.nodes.items()
                           if key in {"source", "batch", "down", "low"}}
    project.graph.connections = project.graph.connections[:3]
    project.graph.nodes["low"] = Node(
        id="low", component="orpheus.builtin.device_out", params={"channels": 2},
    )
    with pytest.raises(CompileError, match="物理IO端点"):
        compiler.compile(project)


@pytest.mark.parametrize("kind", ["src", "split", "window"])
def test_audio_adapters_dynamic_generated_parity(kind):
    from editor_client import EditorClient as TestClient

    from orpheus_core.server.app import create_app

    name = f"test_audio_adapters_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            directory = ROOT / "workspace" / name
            samples = [
                int(12000 * math.sin(2 * math.pi * 1000 * frame / 48000))
                for frame in range(9984)
            ]
            with wave.open(str(directory / "input.wav"), "wb") as stream:
                stream.setnchannels(2)
                stream.setsampwidth(2)
                stream.setframerate(48000)
                stream.writeframes(b"".join(struct.pack("<hh", value, -value) for value in samples))
            project = project_for(kind)
            document = client.get(f"/api/projects/{name}").json()
            document["tasks"] = [{"id": "default", "sample_rate": 48000, "block_size": 32}]
            document["graph"] = {
                "nodes": [
                    {"id": node.id, "component": node.component, "params": node.params}
                    for node in project.graph.nodes.values()
                ],
                "connections": [
                    {"from": str(edge.from_ref), "to": str(edge.to_ref)}
                    for edge in project.graph.connections
                ],
            }
            document["graph"]["nodes"][0] = {
                "id": "source", "component": "orpheus.builtin.wav_in",
                "params": {"channels": 2, "file_path": "input.wav"},
            }
            document["graph"]["nodes"][-1] = {
                "id": "consumer", "component": "orpheus.builtin.wav_out",
                "params": {"channels": 2, "file_path": "outputs/out.wav", "sample_rate": 48000},
            }
            saved = client.put(f"/api/projects/{name}", json=document)
            assert saved.status_code == 200, saved.text
            result = client.post(f"/api/projects/{name}/run").json()
            assert result["status"] == "ok", result.get("stderr", result)
            path = directory / "outputs" / "out.wav"
            dynamic = path.read_bytes()
            with wave.open(str(path), "rb") as stream:
                assert stream.getframerate() == 48000
                assert stream.getnframes() == len(samples) * (2 if kind == "window" else 1)
                pcm = struct.unpack(f"<{stream.getnframes()*2}h", stream.readframes(stream.getnframes()))
                assert max(abs(value) for value in pcm) > 1000
                if kind == "split":
                    expected = samples[:len(samples)-96]
                    assert max(abs(pcm[2*(i+96)]-value) for i, value in enumerate(expected)) <= 2
            result = client.post(f"/api/projects/{name}/run_generated").json()
            assert result["status"] == "ok", result.get("stderr", result)
            assert path.read_bytes() == dynamic
        finally:
            client.delete(f"/api/projects/{name}")
