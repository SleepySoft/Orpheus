"""宽通道统一硬门：动态/生成的全局及Task入口，名称与原生槽访问。"""
from dataclasses import asdict
import json
from pathlib import Path
import struct
import subprocess
import wave

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
BLOCKS = [2, 3, 1, 2, 1]


def verify_mux_gates(tmp_path, cases):
    from test_generated_double_bank import _build

    frames, width = sum(BLOCKS)*32, max(cases.values())
    samples = [[((n*97+c*577) % 12001)-6000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "宽通道硬门验收"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, count in cases.items():
        nodes[name+"_route"] = Node(
            id=name+"_route", component="orpheus.builtin.channel_router",
            params={"channels_in": width, "channels_out": count,
                    "indices": ",".join(map(str, range(count)))})
        nodes[name] = Node(id=name, component="orpheus.builtin.n_way_mux",
                           params={"channels": count, "inputs": 2, "select_index": 1, "ramp_ms": 0.0})
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out",
                                 params={"channels": count, "file_path": name+".wav"})
        for left, right, pin in (("source", name+"_route", "in"),
                                  (name+"_route", name, "in1"), (name, name+"_out", "in")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, pin)))
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/mux_test.c").write_bytes((ROOT / "tests/mux_gate_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(mux_test src/mux_test.c)\n"
                     "target_compile_definitions(mux_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(mux_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(
        ["cmake", "--build", str(ROOT / "build"), "--target", "orpheus_builtin_n_way_mux",
         "orpheus_builtin_channel_router", "orpheus_builtin_wav_in", "orpheus_builtin_wav_out",
         "mux_gate_runner"], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args = []
    for name in cases:
        entry = next(e for e in plan.id_map if e["node"] == name and e["key"] == "select_index")
        args += [name, str(entry["id"])]
    references = {}
    for mode in ("global", "task"):
        for command in (
            [str(ROOT / "build/tests/mux_gate_runner.exe"), str(plan_path),
             str(ROOT / "build/components"), mode, *args],
            [str(generated / "build/mux_test.exe"), mode, *args],
        ):
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            for name, count in cases.items():
                output = tmp_path / (name+".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                expected, cursor = [], 0
                for step, blocks in enumerate(BLOCKS):
                    for row in samples[cursor:cursor+32*blocks]:
                        expected.extend(0 if step % 2 else int(x/32768*32767) for x in row[:count])
                    cursor += 32*blocks
                assert max(abs(a-b) for a, b in zip(actual, expected)) <= 1, (mode, name)
                cursor = 0
                for step, blocks in enumerate(BLOCKS):
                    if step % 2:
                        assert not any(actual[cursor*count:(cursor+32*blocks)*count])
                    cursor += 32*blocks


def test_wide_mux_four_entries(tmp_path):
    verify_mux_gates(tmp_path, {"gate40": 40, "gate52": 52, "gate64": 64})
