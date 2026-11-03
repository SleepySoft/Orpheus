"""共享池延迟：按组读写、变长重解释旧池、四入口参考验收。"""
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
BLOCKS = [1, 1, 1, 30, 2, 4]


def pool_reference(samples, channels, capacity, gated=False):
    pool, cursor, absolute = [0] * capacity, 0, 0
    result = []
    for step, blocks in enumerate(BLOCKS):
        vector = (
            [0] * channels if step in (0, 4) else
            [(c+2) % (capacity+1) for c in range(channels)] if step == 1 else
            [(4 if c == 0 else c+2) % (capacity+1) for c in range(channels)] if step == 2 else
            [(c*7+33) % (capacity+1) for c in range(channels)] if step == 3 else
            [capacity] * channels
        )
        for _ in range(blocks):
            if gated and step not in (0, 3, 5):
                result.extend(value for row in samples[absolute:absolute+32] for value in row[:channels])
                absolute += 32
                continue
            output, prefix = [[0] * channels for _ in range(32)], 0
            for c, delay in enumerate(vector):
                old = [pool[(cursor+prefix+n+1) % capacity] for n in range(min(32, delay))]
                column = old + [samples[absolute+n][c] for n in range(32-len(old))]
                for n, value in enumerate(column):
                    output[n][c] = value
                prefix += delay
            cursor = (cursor+32) % capacity
            prefix = 0
            for c, delay in enumerate(vector):
                for n in range(max(0, 32-delay), 32):
                    pool[(cursor+prefix+delay-31+n) % capacity] = samples[absolute+n][c]
                prefix += delay
            result.extend(value for row in output for value in row)
            absolute += 32
    return result


def verify_pool_delay_cases(tmp_path, cases):
    from test_generated_double_bank import _build

    frames, width = sum(BLOCKS) * 32, max(p["channels"] for p in cases.values())
    samples = [[((n*97+c*577) % 12001)-6000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "共享池延迟验收"})
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
        nodes[name] = Node(id=name, component="orpheus.builtin.pool_delay", params=params)
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
    (generated / "src/pool_delay_test.c").write_bytes((ROOT / "tests/pool_delay_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(pool_delay_test src/pool_delay_test.c)\n"
                     "target_compile_definitions(pool_delay_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(pool_delay_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(
        ["cmake", "--build", str(ROOT / "build"), "--target",
         "orpheus_builtin_pool_delay", "pool_delay_runner"],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 0, result.stdout + result.stderr
    _build(generated, tmp_path)
    args = []
    for name, params in cases.items():
        entry = next(e for e in plan.id_map if e["node"] == name and e["key"] == "delays_i32")
        assert entry["type"] == "int" and entry["count"] == params["channels"]
        args += [name, str(entry["id"])]
    references = {}
    for mode in ("global", "task"):
        commands = [
            [str(ROOT / "build/tests/pool_delay_runner.exe"), str(path),
             str(ROOT / "build/components"), mode, *args],
            [str(generated / "build/pool_delay_test.exe"), mode, *args],
        ]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True,
                                    text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            for name, params in cases.items():
                data = (tmp_path / (name + ".wav")).read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                with wave.open(str(tmp_path / (name + ".wav")), "rb") as stream:
                    count = params["channels"]
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                reference = (pool_reference(samples, count, params["pool_size_samples"], gated=name == "gated") if params.get("enabled", True)
                             else [value for row in samples for value in row[:count]])
                expected = [int(value*32767/32768) for value in reference]
                assert max(abs(a-b) for a, b in zip(actual, expected)) <= 1, (mode, name)


def test_pool_delay_four_entries(tmp_path):
    verify_pool_delay_cases(tmp_path, {
        "row1": {"channels": 16, "pool_size_samples": 600},
        "row2": {"channels": 16, "pool_size_samples": 600},
        "small": {"channels": 4, "pool_size_samples": 11},
        "mono": {"channels": 1, "pool_size_samples": 1},
        "bypass": {"channels": 22, "pool_size_samples": 512, "enabled": False},
        "gated": {"channels": 22, "pool_size_samples": 512},
    })
