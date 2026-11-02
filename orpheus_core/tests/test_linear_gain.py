"""线性目标、逐帧平滑、真实参数调用以及动态/生成四入口验收。"""
from dataclasses import asdict
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import wave

import yaml

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
BLOCKS = [5, 3, 7, 5, 4]
TARGETS = [None, -1.25, 0.0, 0.625, -0.0]


def test_gain_manifest_preserves_legacy_parameter_order():
    manifest = yaml.safe_load(
        (ROOT / "components/orpheus/builtin/gain/component.yaml").read_text(encoding="utf-8"),
    )
    assert [p["id"] for p in manifest["parameters"][:3]] == ["gain_db", "channels", "smoothing_ms"]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def verify_linear_gain_cases(tmp_path, cases):
    from test_generated_double_bank import _build

    frames = sum(BLOCKS) * 32
    width = max(p["channels"] for p in cases.values())
    samples = [[((n * 97 + c * 577) % 6001) - 3000 for c in range(width)]
               for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "线性增益验收"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name + "_route"] = Node(id=name + "_route", component="orpheus.builtin.channel_router",
                                     params={"channels_in": width, "channels_out": count,
                                             "indices": ",".join(str(c) for c in range(count))})
        nodes[name] = Node(id=name, component="orpheus.builtin.gain", params=params)
        nodes[name + "_out"] = Node(id=name + "_out", component="orpheus.builtin.wav_out",
                                   params={"channels": count, "file_path": name + ".wav"})
        for left, right in (("source", name + "_route"), (name + "_route", name),
                            (name, name + "_out")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
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
    shutil.copyfile(ROOT / "tests/linear_gain_runner.cpp", generated / "src/gain_test.c")
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(gain_test src/gain_test.c)\n"
                     "target_compile_definitions(gain_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(gain_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                             "linear_gain_runner"], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    _build(generated, tmp_path)
    references = {}
    for mode in ("global", "task"):
        commands = [
            [str(ROOT / "build/tests/linear_gain_runner.exe"), str(plan_path),
             str(ROOT / "build/components"), mode, *cases],
            [str(generated / "build/gain_test.exe"), mode, *cases],
        ]
        for command in commands:
            for name in cases:
                (tmp_path / (name + ".wav")).unlink(missing_ok=True)
            result = subprocess.run(command, cwd=tmp_path, capture_output=True,
                                    text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            for name, params in cases.items():
                output = tmp_path / (name + ".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels(), stream.getframerate()) == (
                        frames, count, 48000,
                    )
                    actual = struct.unpack(f"<{frames * count}h", stream.readframes(frames))
                state, expected, cursor = 0.0, [], 0
                b0, a1 = f32(params.get("linear_b0", 1.0)), f32(params.get("linear_a1", 0.0))
                for blocks, update in zip(BLOCKS, TARGETS):
                    target = f32(params.get("gain_linear", 1.0) if update is None else update)
                    for n in range(cursor, cursor + blocks * 32):
                        if params.get("gain_mode", 0) == 0:
                            gain = math.pow(10, params.get("gain_db", 0) / 20)
                        elif b0 == 1.0 and a1 == 0.0:
                            gain = target
                        else:
                            gain = f32(f32(b0 * target) + state)
                            state = f32(0.0 - f32(a1 * gain))
                        for c in range(count):
                            sample = f32(samples[n][c] / 32768 * gain)
                            expected.append(int(max(-1, min(1, sample)) * 32767))
                    cursor += blocks * 32
                assert max(abs(a - b) for a, b in zip(actual, expected)) <= 1, name


def test_linear_gain_runtime_updates_four_entries(tmp_path):
    verify_linear_gain_cases(tmp_path, {
        "smooth": {"channels": 10, "gain_mode": 1, "gain_linear": 0.75,
                   "linear_b0": 0.25, "linear_a1": -0.75},
        "direct": {"channels": 3, "gain_mode": 1, "gain_linear": -0.5},
        "zero": {"channels": 1, "gain_mode": 1, "gain_linear": 0.0},
        "legacy": {"channels": 2, "gain_db": -6.0},
    })
