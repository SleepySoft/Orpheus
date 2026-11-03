"""逐位验证布局桥接；256/768槽不是256/768个新时间样本。"""
from dataclasses import asdict
import json
from pathlib import Path
import shutil
import struct
import subprocess
import wave

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).parent / "fixtures/halfcomplex"


def build_project(cases, batches):
    width = max(p["channels"] for p in cases.values())
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                            params={"channels": width, "file_path": "input.wav"})}
    edges = []

    def edge(a, b):
        edges.append(Connection(PortRef.parse(a), PortRef.parse(b)))

    for name, config in cases.items():
        p = {k: v for k, v in config.items() if k not in ("typed", "tick")}
        common = {k: p[k] for k in ("channels", "fft_size", "batches", "bins")}
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": p["channels"],
                                            "indices": ",".join(map(str, range(p["channels"])))})
        nodes[name+"_frames"] = Node(id=name+"_frames", component="orpheus.builtin.circular_buffer",
                                     params={"channels": p["channels"], "frame_size": 256, "hop_size": 128, "num_frames": batches})
        nodes[name+"_fft"] = Node(id=name+"_fft", component="orpheus.builtin.rfft",
                                  params={"channels": p["channels"], "fft_size": 256})
        nodes[name] = Node(id=name, component="orpheus.builtin.halfcomplex_decode", params=p)
        nodes[name+"_capture"] = Node(id=name+"_capture", component="orpheus.test.halfcomplex_capture", params=common)
        edge("source:out", name+"_route:in")
        edge(name+"_route:out", name+"_frames:in")
        edge(name+"_frames:out", name+"_fft:in")
        typed = config.get("typed", False)
        spectrum = name+"_fft:"+("spectrum" if typed else "out")
        edge(spectrum, name+":"+("spectrum" if typed else "in"))
        edge(spectrum, name+"_capture:"+("spectrum" if typed else "packed"))
        edge(name+":out", name+"_capture:values")
        if config.get("tick"):
            edge(name+":tick", name+"_capture:tick")
    project = Project(metadata={"name": "半复数布局桥接"}, tasks={
        "default": Task(id="default", sample_rate=48000, block_size=128*batches)})
    project.graph = Graph(nodes=nodes, connections=edges)
    return project


def verify_halfcomplex_decode(tmp_path, cases, batches):
    from test_generated_double_bank import _build
    width = max(p["channels"] for p in cases.values())
    step = 128*batches
    samples = [((n*113+c*911) % 50001)-25000 for n in range(step*3) for c in range(width)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    registry = Registry([ROOT / "components"]); registry.scan()
    registry.add_manifest(FIXTURE / "component.yaml")
    project = build_project(cases, batches)
    plan = GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"] == step
    assert set(plan.schedule["periods"].values()) == {1}
    for name, p in cases.items():
        assert plan.node_configs[name]["frame_step"] == step
        assert plan.node_configs[name+"_capture"]["frame_step"] == step
        decoded = next(b for b in plan.buffers.values() if b["from"] == name+":out")
        assert (decoded["type"], decoded["channels"], decoded["frame_count"]) == ("bulk", p["channels"], p["bins"]*2*batches)
        assert plan.execution_order.index(name)<plan.execution_order.index(name+"_capture")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/halfcomplex_test.c").write_bytes((ROOT / "tests/halfcomplex_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as file:
        file.write("\nadd_executable(halfcomplex_test src/halfcomplex_test.c)\n"
                   "target_compile_definitions(halfcomplex_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                   "target_link_libraries(halfcomplex_test PRIVATE orpheus_graph)\n"
                   f'add_library(capture_dynamic SHARED "{(FIXTURE / "src/capture.c").as_posix()}")\n'
                   f'target_include_directories(capture_dynamic PRIVATE "{(FIXTURE / "include").as_posix()}")\n'
                   "target_compile_definitions(capture_dynamic PRIVATE ORPHEUS_BUILDING_COMPONENT)\n"
                   'set_target_properties(capture_dynamic PROPERTIES PREFIX "lib" OUTPUT_NAME "orpheus_test_halfcomplex_capture" '
                   'WINDOWS_EXPORT_ALL_SYMBOLS ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
                   'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")\n')
    components = ("wav_in", "channel_router", "circular_buffer", "rfft", "halfcomplex_decode")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "halfcomplex_runner",
                             *["orpheus_builtin_"+c for c in components]], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    libraries = generated / "build/components"
    for component in components:
        for library in (ROOT / "build/components").glob("*orpheus_builtin_"+component+".*"):
            if library.suffix in (".dll", ".so", ".dylib"):
                shutil.copy2(library, libraries / library.name)
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    arguments = []
    for name, p in cases.items():
        arguments += [name+"_capture", str(p["fft_size"]*batches*p["channels"]),
                      str(p["bins"]*2*batches*p["channels"]), str(plan.node_configs[name+"_capture"]["frames"])]
    baseline = None
    for mode in ("global", "task"):
        commands = [[str(ROOT / "build/tests/halfcomplex_runner.exe"), str(plan_path), str(libraries), mode, str(step), *arguments],
                    [str(generated / "build/halfcomplex_test.exe"), mode, str(step), *arguments]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            data = (tmp_path / "halfcomplex.bin").read_bytes()
            if baseline is None:
                baseline = data
            assert data == baseline
            cursor = 0
            for block in range(3):
                for name, p in cases.items():
                    channels, fft_size, bins, first = (p[k] for k in ("channels", "fft_size", "bins", "first_bin"))
                    size = fft_size*batches*channels*4
                    packed = data[cursor:cursor+size]; cursor += size
                    expected = bytearray()
                    for h in range(batches):
                        for k in range(first, first+bins):
                            for component in range(2):
                                for c in range(channels):
                                    index = (h*fft_size+(k if component==0 else fft_size-k))*channels+c
                                    expected += b"\0\0\0\0" if component and k in (0, fft_size//2) else packed[index*4:index*4+4]
                    assert data[cursor:cursor+len(expected)] == expected, (mode, block, name)
                    cursor += len(expected)
            assert cursor == len(data)


@pytest.mark.parametrize("batches", [1, 3])
def test_halfcomplex_decode_four_entries(tmp_path, batches):
    cases = {name: {"channels": channels, "fft_size": 256, "batches": batches,
                    "first_bin": first, "bins": bins, "typed": typed, "tick": name=="band"}
             for name, channels, first, bins, typed in
             (("full", 2, 0, 129, False), ("hf", 10, 0, 129, False),
              ("typed", 2, 0, 129, True), ("band", 2, 5, 30, True))}
    verify_halfcomplex_decode(tmp_path, cases, batches)


def test_halfcomplex_decode_requires_exactly_one_input():
    registry = Registry([ROOT / "components"]); registry.scan()
    registry.add_manifest(FIXTURE / "component.yaml")
    cases = {"decode": {"channels": 2, "fft_size": 256, "batches": 1, "first_bin": 0, "bins": 129}}
    project = build_project(cases, 1)
    project.graph.connections.append(Connection(PortRef("decode_fft", "spectrum"), PortRef("decode", "spectrum")))
    with pytest.raises(CompileError, match="输入互斥"):
        GraphCompiler(registry).compile(project)
