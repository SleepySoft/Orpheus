"""矩阵真实参数槽：逐项float32累计、全容量与四入口整组系数更新。"""
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
BLOCKS = [2, 1, 2, 1]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def verify_matrix_bulk(tmp_path, cases):
    from test_generated_double_bank import _build

    frames, width = sum(BLOCKS)*32, max(p["cols"] for p in cases.values())
    samples = [[((n*97+c*577) % 12001)-6000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "矩阵参数槽验收"},
                      tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": params["cols"],
                                            "indices": ",".join(map(str, range(params["cols"])))})
        nodes[name] = Node(id=name, component="orpheus.builtin.matrix_mul", params=params)
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out",
                                  params={"channels": params["rows"], "file_path": name+".wav"})
        for left, right in (("source", name+"_route"), (name+"_route", name), (name, name+"_out")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry(); registry.add_search_path(ROOT / "components"); registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    path = tmp_path / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/matrix_test.c").write_bytes((ROOT / "tests/matrix_bulk_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(matrix_test src/matrix_test.c)\n"
                     "target_compile_definitions(matrix_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(matrix_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                             "orpheus_builtin_matrix_mul", "matrix_bulk_runner", "orpheus_builtin_channel_router",
                             "orpheus_builtin_wav_in", "orpheus_builtin_wav_out"],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args, expected = [], {}
    for name, params in cases.items():
        rows, cols = params["rows"], params["cols"]
        slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == "matrix")
        assert slot["form"] == "BULK" and slot["count"] == rows*cols and slot["type"] == "float"
        args += [name, str(slot["id"]), str(rows*cols)]
        matrix = [f32(float(v)) for v in params["matrix"].split(",")]
        reference, cursor = [], 0
        for step, blocks in enumerate(BLOCKS):
            vector = [f32(v*(0.5 if step == 1 else 0 if step == 2 else 1)) for v in matrix]
            for frame in samples[cursor:cursor+blocks*32]:
                for row in range(rows):
                    total = 0.0
                    for c in range(cols):
                        total = f32(total+f32((frame[c]/32768)*vector[row*cols+c]))
                    reference.append(int(f32(total*32767)))
            cursor += blocks*32
        expected[name] = reference
    references = {}
    for mode in ("global", "task"):
        commands = [[str(ROOT / "build/tests/matrix_bulk_runner.exe"), str(path),
                     str(ROOT / "build/components"), mode, *args],
                    [str(generated / "build/matrix_test.exe"), mode, *args]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            for name, params in cases.items():
                output = tmp_path / (name+".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["rows"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                assert max(abs(a-b) for a, b in zip(actual, expected[name])) <= 1, (mode, name)


def test_matrix_bulk_four_entries(tmp_path):
    verify_matrix_bulk(tmp_path, {
        "wide": {"rows": 32, "cols": 32, "matrix": ",".join(str((c % 3-1)/64) for c in range(1024))},
        "ordered": {"rows": 1, "cols": 6, "matrix": "0.5,-0.25,0,0.125,-0.5,0.25"},
        "single": {"rows": 1, "cols": 1, "matrix": "-0.5"},
    })
