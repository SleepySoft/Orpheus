"""批次FFT、具名频谱数据及Overlap-Add的编译与双路径验收。"""

from __future__ import annotations

import json
import math
import shutil
import struct
import subprocess
import wave
from dataclasses import asdict
from pathlib import Path

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, ProjectLoader, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).parent / "fixtures" / "spectral_gate"


@pytest.fixture(scope="module")
def compiler():
    registry = Registry([ROOT / "components"])
    registry.scan()
    registry.add_manifest(FIXTURE / "component.yaml")
    return GraphCompiler(registry)


def edge(source: str, target: str) -> Connection:
    return Connection(PortRef.parse(source), PortRef.parse(target))


def spectral_project(batch: int = 1, *, legacy: bool = False, channels: int = 2) -> Project:
    project = Project(metadata={"name": "频域闭环"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=128 * batch)}
    nodes = [
        Node(id="source", component="orpheus.builtin.signal_gen", params={"channels": 2}),
        Node(id="frames", component="orpheus.builtin.circular_buffer",
             params={"channels": 2, "frame_size": 256, "hop_size": 128, "num_frames": batch}),
        Node(id="fft", component="orpheus.builtin.rfft", params={"channels": 2, "fft_size": 256}),
        Node(id="inverse", component="orpheus.builtin.ifft", params={"channels": 2, "fft_size": 256}),
        Node(id="ola", component="orpheus.builtin.overlap_add",
             params={"channels": 2, "frame_size": 256, "hop_size": 128, "num_frames": batch}),
        Node(id="output", component="orpheus.builtin.gain",
             params={"channels": 2, "gain_db": 0.0, "smoothing_ms": 0.0}),
    ]
    connections = [edge("source:out", "frames:in"), edge("frames:out", "fft:in"),
                   edge("inverse:out", "ola:in"), edge("ola:out", "output:in")]
    connections.append(edge("fft:out", "inverse:in") if legacy
                       else edge("fft:spectrum", "inverse:spectrum"))
    for node in nodes:
        node.params["channels"] = channels
    project.graph = Graph(nodes={node.id: node for node in nodes}, connections=connections)
    return project


def add_spectral_gate(project: Project, batch: int, channels: int) -> None:
    project.graph.nodes["gate"] = Node(
        id="gate", component="orpheus.test.spectral_gate",
        params={"channels": channels, "fft_size": 256, "num_frames": batch},
    )
    project.graph.connections = [conn for conn in project.graph.connections
                                 if str(conn.to_ref) != "inverse:spectrum"]
    project.graph.connections.extend([
        edge("source:out", "gate:clock"), edge("fft:spectrum", "gate:spectrum"),
        edge("gate:out", "inverse:spectrum"),
    ])


@pytest.mark.parametrize("batch", [1, 3])
def test_spectral_data_shapes_preserve_hop_timeline(compiler, batch):
    plan = compiler.compile(spectral_project(batch))
    assert plan.schedule["tick"] == 128 * batch
    assert set(plan.schedule["periods"].values()) == {1}
    for node in ("fft", "inverse", "ola"):
        assert plan.node_configs[node]["frames"] == 256 * batch
        assert plan.node_configs[node]["frame_step"] == 128 * batch
    assert plan.node_configs["output"]["frames"] == 128 * batch
    spectrum = next(buf for buf in plan.buffers.values() if buf["to"] == "inverse:spectrum")
    assert spectrum["type"] == "bulk"
    assert spectrum["layout"] == "halfcomplex"
    assert spectrum["frame_size"] == 256
    assert spectrum["frame_count"] == 256 * batch


def test_ifft_input_alternatives_are_explicit(compiler):
    project = spectral_project()
    project.graph.connections.append(edge("fft:out", "inverse:in"))
    with pytest.raises(CompileError, match="输入互斥"):
        compiler.compile(project)


def test_mismatched_fft_frame_sizes_are_rejected(compiler):
    project = spectral_project(3)
    project.graph.nodes["inverse"].params["fft_size"] = 128
    with pytest.raises(CompileError, match="帧结构"):
        compiler.compile(project)


def test_legacy_fft_ports_still_compile(compiler):
    plan = compiler.compile(spectral_project(3, legacy=True))
    assert plan.node_configs["inverse"]["frames"] == 768
    assert plan.node_configs["output"]["frames"] == 384


def test_ifft_requires_one_input(compiler):
    project = spectral_project()
    project.graph.connections = [conn for conn in project.graph.connections
                                 if conn.to_ref.node_id != "inverse"]
    with pytest.raises(CompileError, match="输入互斥"):
        compiler.compile(project)


def test_equal_lengths_do_not_imply_equal_spectral_layout(compiler, monkeypatch):
    port = next(port for port in compiler.registry.get("orpheus.builtin.rfft").manifest["ports"]
                if port["id"] == "spectrum")
    monkeypatch.setitem(port, "layout", "complex_interleaved")
    with pytest.raises(CompileError, match="数据布局"):
        compiler.compile(spectral_project())


@pytest.mark.parametrize("field", ["layout", "frame_size"])
def test_spectral_contract_cannot_be_implicitly_inferred(compiler, monkeypatch, field):
    port = next(port for port in compiler.registry.get("orpheus.builtin.rfft").manifest["ports"]
                if port["id"] == "spectrum")
    monkeypatch.delitem(port, field)
    with pytest.raises(CompileError, match="数据布局/帧结构"):
        compiler.compile(spectral_project())


@pytest.mark.parametrize("expression", ["not_an_expression", "param:missing"])
def test_invalid_frame_structure_reports_compile_error(compiler, monkeypatch, expression):
    port = next(port for port in compiler.registry.get("orpheus.builtin.rfft").manifest["ports"]
                if port["id"] == "spectrum")
    monkeypatch.setitem(port, "frame_size", expression)
    with pytest.raises(CompileError, match="无法求值帧结构"):
        compiler.compile(spectral_project())


def test_non_integral_spectral_batch_is_rejected(compiler):
    project = spectral_project(3)
    project.graph.nodes["inverse"].params["fft_size"] = 512
    with pytest.raises(CompileError, match="帧结构.*整除"):
        compiler.compile(project)


def test_spectral_shape_does_not_depend_on_node_declaration_order(compiler):
    project = spectral_project(3)
    expected = compiler.compile(project)
    project.graph.nodes = dict(reversed(list(project.graph.nodes.items())))
    actual = compiler.compile(project)
    assert actual.node_configs == expected.node_configs
    assert actual.schedule == expected.schedule


def test_single_frame_auto_size_still_works(compiler):
    project = spectral_project()
    for node in ("fft", "inverse"):
        project.graph.nodes[node].params["fft_size"] = 0
    plan = compiler.compile(project)
    spectrum = next(buf for buf in plan.buffers.values() if buf["to"] == "inverse:spectrum")
    assert spectrum["frame_size"] == 256
    assert plan.node_configs["output"]["frames"] == 128


def test_spectral_example_uses_declared_parameters(compiler):
    project = ProjectLoader(compiler.registry).load(ROOT / "examples" / "spectral_roundtrip.yaml")
    for node in project.graph.nodes.values():
        declared = {param["id"] for param in compiler.registry.get(node.component).manifest["parameters"]}
        assert node.params.keys() <= declared
    plan = compiler.compile(project)
    assert plan.schedule["tick"] == 384
    assert plan.node_configs["sink"]["frames"] == 384


def test_data_payload_can_differ_from_processing_quantum(compiler):
    project = spectral_project()
    add_spectral_gate(project, 1, 2)
    project.graph.nodes["inverse"].params["fft_size"] = 0
    plan = compiler.compile(project)
    assert plan.node_configs["inverse"]["frames"] == 128
    assert plan.node_configs["inverse"]["output_port_block_sizes"] == {"out": 256}
    assert plan.node_configs["ola"]["frames"] == 256
    assert plan.node_configs["ola"]["frame_step"] == 128
    assert plan.node_configs["output"]["frames"] == 128
    assert set(plan.schedule["periods"].values()) == {1}


@pytest.mark.parametrize("batch,legacy,channels", [
    (1, False, 6), (3, False, 8), (3, True, 2), (1, True, 2),
])
def test_spectral_roundtrip_dynamic_generated_global_and_task(
    compiler, tmp_path, batch, legacy, channels,
):
    from array_graph_runner import use_linear_call_chain
    from test_generated_double_bank import _build

    project = spectral_project(batch, legacy=legacy, channels=channels)
    project.graph.nodes["source"] = Node(
        id="source", component="orpheus.builtin.wav_in",
        params={"channels": channels, "file_path": "input.wav"},
    )
    project.graph.nodes["output"].params["gain_db"] = -6.020599913279624
    project.graph.nodes["sink"] = Node(
        id="sink", component="orpheus.builtin.wav_out",
        params={"channels": channels, "file_path": "out.wav"},
    )
    project.graph.connections.append(edge("output:out", "sink:in"))
    if not legacy:
        project.graph.nodes["fft"].params["output_mode"] = "power"
        add_spectral_gate(project, batch, channels)
    elif batch == 1:
        for node in ("fft", "inverse"):
            project.graph.nodes[node].params.pop("fft_size")
    signal = [
        tuple(int(10000 * math.sin(2 * math.pi * (777 + 311 * channel) * index / 48000))
              + 300 * channel for channel in range(channels))
        for index in range(128 * batch * 8)
    ]
    silence = (0,) * channels
    samples = signal + [silence] * (128 * batch)
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(channels)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{channels}h", *sample) for sample in samples))
    project.graph.nodes = dict(reversed(list(project.graph.nodes.items())))
    plan = compiler.compile(project)
    use_linear_call_chain(plan)
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(compiler.registry, ROOT).generate(plan, generated)
    if not legacy:
        cmake = generated / "CMakeLists.txt"
        cmake.write_text(
            cmake.read_text(encoding="utf-8") + "\n"
            + f'add_library(spectral_gate_dynamic SHARED "{(FIXTURE / "src" / "spectral_gate.c").as_posix()}")\n'
            + f'target_include_directories(spectral_gate_dynamic PRIVATE "{(FIXTURE / "include").as_posix()}" '
            + f'"{(ROOT / "orpheus_abi" / "include").as_posix()}")\n'
            + 'target_compile_definitions(spectral_gate_dynamic PRIVATE ORPHEUS_BUILDING_COMPONENT)\n'
            + 'set_target_properties(spectral_gate_dynamic PROPERTIES PREFIX "lib" '
              'OUTPUT_NAME "orpheus_test_spectral_gate" '
              'RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
              'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")\n'
            + 'if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")\n'
            + '  target_compile_options(spectral_gate_dynamic PRIVATE -Wall -Wextra -Werror)\n'
            + '  target_compile_options(orpheus_test_spectral_gate PRIVATE -Wall -Wextra -Werror)\n'
            + 'endif()\n',
            encoding="utf-8",
        )
    executable = _build(generated, tmp_path)
    dynamic_executable = ROOT / "build" / "orpheus_runtime.exe"
    components = ROOT / "build" / "components"
    if not legacy:
        private_components = generated / "build" / "components"
        for config in plan.node_configs.values():
            if config["component"] == "orpheus.test.spectral_gate":
                continue
            for library in components.glob(f"*{config['component'].replace('.', '_')}.*"):
                if library.suffix in (".dll", ".so", ".dylib"):
                    shutil.copy2(library, private_components / library.name)
        components = private_components
        assert plan.node_configs["inverse"]["frames"] == 128 * batch
        assert plan.node_configs["inverse"]["output_port_block_sizes"] == {"out": 256 * batch}
    commands = [
        [str(dynamic_executable), str(plan_path), str(components)],
        [str(executable), "9"],
        [str(dynamic_executable), str(plan_path), str(components), "--task", "default", "9"],
        [str(executable), "0", "--task", "default", "9"],
    ]
    expected = [silence] * 128 + signal + [silence] * (128 * batch - 128)
    reference = None
    output = tmp_path / "out.wav"
    for command in commands:
        if output.exists():
            output.unlink()
        result = subprocess.run(command, cwd=tmp_path, capture_output=True,
                                text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        assert output.exists(), result.stdout + result.stderr
        data = output.read_bytes()
        if reference is None:
            reference = data
        else:
            assert data == reference
        with wave.open(str(output), "rb") as stream:
            assert (stream.getnchannels(), stream.getframerate(), stream.getnframes()) == (
                channels, 48000, len(expected),
            )
            pcm = struct.unpack(f"<{channels * len(expected)}h", stream.readframes(len(expected)))
        flat_expected = [value for sample in expected for value in sample]
        assert max(abs(value) for value in pcm) > 5000
        assert max(abs(actual - wanted) for actual, wanted in zip(pcm, flat_expected)) <= 2
