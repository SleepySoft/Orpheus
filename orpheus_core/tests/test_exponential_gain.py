"""指数增益核：同目标重请求、重入、吸附和四入口逐位状态参考。"""
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
REQUESTS = [(0.25, 0.98, 3, 2), (0.25, 0.995, 2, 2), (0.8, 1.015, 2, 1),
            (0.2, 0.99, 0, 1), (0.8, 0.98, 2, 1), (0.8, 1, 1, 2)]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def exponential_reference(samples, params):
    current = f32(params.get("initial_gain", 1.0))
    minimum = f32(params.get("min_gain", 4.7315126e-7))
    threshold = f32(params.get("snap_threshold", 1.0e-9))
    previous, memory, load = 0.0, 0.0, True
    expected, states, cursor = [], [], 0
    for target, coefficient, remaining, blocks in REQUESTS:
        target, coefficient = f32(target), f32(coefficient)
        for _ in range(blocks):
            start = max(current, minimum)
            ramping = abs(f32(target-start)) > threshold and remaining > 0
            if ramping:
                remaining -= 1
                if load or f32(target-previous) != 0:
                    memory = f32(coefficient*start)
            for frame in samples[cursor:cursor+32]:
                current = memory if ramping else target
                if ramping:
                    memory = f32(0.0-f32(-coefficient*current))
                for x in frame[:params["channels"]]:
                    expected.append(int(f32(f32((x/32768)*current)*32767)))
            if ramping:
                previous, load = target, False
            states += [current, float(remaining)]
            cursor += 32
    return expected, states


def verify_exponential_gain(tmp_path, cases):
    from test_generated_double_bank import _build

    frames, width = sum(r[-1] for r in REQUESTS)*32, max(p["channels"] for p in cases.values())
    samples = [[((n*97+c*577) % 12001)-6000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "指数增益斜坡核验收"},
                      tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": count,
                                            "indices": ",".join(map(str, range(count)))})
        nodes[name] = Node(id=name, component="orpheus.builtin.exponential_gain", params=params)
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
    (generated / "src/gain_test.c").write_bytes((ROOT / "tests/exponential_gain_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(gain_test src/gain_test.c)\n"
                     "target_compile_definitions(gain_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(gain_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                             "orpheus_builtin_exponential_gain", "exponential_gain_runner",
                             "orpheus_builtin_channel_router", "orpheus_builtin_wav_in",
                             "orpheus_builtin_wav_out"], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args = []
    for name in cases:
        args.append(name)
        for key in ("target_gain", "ramp_coefficient", "remaining_blocks"):
            slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == key)
            assert slot["form"] == "SCALAR" and slot["type"] == "float" and slot["kind"] == "RTC"
            args.append(str(slot["id"]))
    expected = {name: exponential_reference(samples, params) for name, params in cases.items()}
    expected_states = b"".join(struct.pack("<2f", *expected[name][1][i:i+2])
                               for i in range(0, sum(r[-1] for r in REQUESTS)*2, 2)
                               for name in cases)
    references = {}
    for mode in ("global", "task"):
        commands = [[str(ROOT / "build/tests/exponential_gain_runner.exe"), str(path),
                     str(ROOT / "build/components"), mode, *args],
                    [str(generated / "build/gain_test.exe"), mode, *args]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            assert (tmp_path / "gain_states.bin").read_bytes() == expected_states
            for name, params in cases.items():
                output = tmp_path / (name+".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                assert max(abs(a-b) for a, b in zip(actual, expected[name][0])) <= 1, (mode, name)


def test_exponential_gain_four_entries(tmp_path):
    verify_exponential_gain(tmp_path, {
        "wide": {"channels": 64, "initial_gain": 1.0},
        "floor_start": {"channels": 3, "initial_gain": 0.0},
    })
