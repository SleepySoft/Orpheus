"""逐通道线性增益的整组参数更新与四入口一致性。"""
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


def verify_channel_gains(tmp_path, cases):
    from test_generated_double_bank import _build

    frames, width = sum(BLOCKS)*32, max(p["channels"] for p in cases.values())
    samples = [[((n*97+c*577) % 12001)-6000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "逐通道增益验收"},
                      tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": count,
                                            "indices": ",".join(map(str, range(count)))})
        nodes[name] = Node(id=name, component="orpheus.builtin.gain", params=params)
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out",
                                  params={"channels": count, "file_path": name+".wav"})
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
    (generated / "src/gain_test.c").write_bytes((ROOT / "tests/channel_gain_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(gain_test src/gain_test.c)\n"
                     "target_compile_definitions(gain_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(gain_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                             "orpheus_builtin_gain", "channel_gain_runner", "orpheus_builtin_channel_router",
                             "orpheus_builtin_wav_in", "orpheus_builtin_wav_out"],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args, expected = [], {}
    for name, params in cases.items():
        count = params["channels"]
        slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == "gains")
        assert slot["form"] == "BULK" and slot["count"] == count and slot["type"] == "float"
        args += [name, str(slot["id"])]
        gains = ([float(v) for v in params["gains"].split(",")] if params.get("gains") else [1.0]*count)
        reference, cursor = [], 0
        for step, blocks in enumerate(BLOCKS):
            vector = [(c % 5-2)/4 for c in range(count)] if step == 1 else [0]*count if step == 2 else gains
            for frame in samples[cursor:cursor+blocks*32]:
                reference += [int((x/32768)*g*32767) for x, g in zip(frame, vector)]
            cursor += blocks*32
        expected[name] = reference
    references = {}
    for mode in ("global", "task"):
        commands = [[str(ROOT / "build/tests/channel_gain_runner.exe"), str(path),
                     str(ROOT / "build/components"), mode, *args],
                    [str(generated / "build/gain_test.exe"), mode, *args]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            for name, params in cases.items():
                output = tmp_path / (name+".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                assert max(abs(a-b) for a, b in zip(actual, expected[name])) <= 1, (mode, name)


def test_channel_gain_four_entries(tmp_path):
    verify_channel_gains(tmp_path, {
        "wide": {"channels": 32, "gain_mode": 2, "gains": ",".join(str((c % 7-3)/4) for c in range(32))},
        "unity": {"channels": 3, "gain_mode": 2},
    })
