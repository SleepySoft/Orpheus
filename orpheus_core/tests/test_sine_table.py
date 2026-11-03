"""周期载波表：有符号纯乘法、通道共相位和四入口一致性。"""
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
BLOCKS = 13


def verify_sine_table_cases(tmp_path, cases):
    from test_generated_double_bank import _build

    frames, width = BLOCKS*32, max(params["channels"] for params in cases.values())
    samples = [[((n*97+c*577) % 12001)-6000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "周期表调制验收"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name + "_route"] = Node(
            id=name + "_route", component="orpheus.builtin.channel_router",
            params={"channels_in": width, "channels_out": count,
                    "indices": ",".join(map(str, range(count)))})
        nodes[name] = Node(id=name, component="orpheus.builtin.sine_mod", params=params)
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
    path = tmp_path / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    result = subprocess.run(
        ["cmake", "--build", str(ROOT / "build"), "--target", "orpheus_builtin_sine_mod", "orpheus_runtime"],
        capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    exe = _build(generated, tmp_path)
    dynamic = [str(ROOT / "build/orpheus_runtime.exe"), str(path), str(ROOT / "build/components")]
    commands = [
        dynamic, [*dynamic, "--task", "default", str(BLOCKS)],
        [str(exe), str(BLOCKS)], [str(exe), "0", "--task", "default", str(BLOCKS)],
    ]
    references = {}
    for command in commands:
        for name in cases:
            (tmp_path / (name + ".wav")).unlink(missing_ok=True)
        result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        for name, params in cases.items():
            path = tmp_path / (name + ".wav")
            data = path.read_bytes()
            assert data == references.setdefault(name, data), name
            count = params["channels"]
            with wave.open(str(path), "rb") as stream:
                assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
            table = [struct.unpack("<f", struct.pack("<f", float(v)))[0]
                     for v in params["carrier_table"].split(",")]
            assert len(table) == params["table_length"]
            expected = [int(samples[n][c] / 32768 * table[n % len(table)] * 32767)
                        for n in range(frames) for c in range(count)]
            assert max(abs(a-b) for a, b in zip(actual, expected)) <= 1, name


def test_sine_table_four_entries(tmp_path):
    verify_sine_table_cases(tmp_path, {
        "shared": {"channels": 10, "modulation_mode": 1, "table_length": 11,
                   "carrier_table": "0,0.5,1,0.75,0.25,0,-0.25,-0.75,-1,-0.5,0"},
        "period_five": {"channels": 3, "modulation_mode": 1, "table_length": 5,
                  "carrier_table": "0,-0,-1,0.5,1"},
        "mono": {"channels": 1, "modulation_mode": 1, "table_length": 1,
                 "carrier_table": "-1"},
    })
