"""整数延迟向量：真实BULK/ID/二进制调用、越界原子拒绝和四入口数值验收。"""
from dataclasses import asdict
import json
from pathlib import Path
import struct
import subprocess
import wave

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, ControlConnection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
BLOCKS = [65, 3, 7, 5, 4, 8]


def verify_integer_bulk_delay_cases(tmp_path, cases, *, control=False):
    from test_generated_double_bank import _build

    frames = (5 if control else sum(BLOCKS)) * 32
    width = max(params["channels"] for params in cases.values())
    samples = [[((n * 97 + c * 577) % 12001) - 6000 for c in range(width)]
               for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "整数BULK延迟验收"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name + "_route"] = Node(
            id=name + "_route", component="orpheus.builtin.channel_router",
            params={"channels_in": width, "channels_out": count,
                    "indices": ",".join(str(c) for c in range(count))},
        )
        nodes[name] = Node(id=name, component="orpheus.builtin.delay_line", params=params)
        nodes[name + "_out"] = Node(id=name + "_out", component="orpheus.builtin.wav_out",
                                   params={"channels": count, "file_path": name + ".wav"})
        for left, right in (("source", name + "_route"), (name + "_route", name),
                            (name, name + "_out")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
    project.graph = Graph(nodes=nodes, connections=edges)
    if control:
        project.control_connections = [
            ControlConnection(PortRef("control_source", "delays_i32"), PortRef("target", "delays_i32"))
        ]
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    for name, params in cases.items():
        for key in ("delays_samples", "delays_i32"):
            entry = next(entry for entry in plan.id_map if entry["node"] == name and entry["key"] == key)
            assert entry["count"] == params["channels"]
        assert next(entry for entry in plan.id_map
                    if entry["node"] == name and entry["key"] == "delays_i32")["type"] == "int"
    if control:
        assert plan.control_links[0]["transport"] == "bulk"
    path = tmp_path / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src" / "integer_bulk_test.c").write_bytes(
        (ROOT / "tests" / "integer_bulk_runner.cpp").read_bytes()
    )
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(integer_bulk_test src/integer_bulk_test.c)\n"
                     "target_compile_definitions(integer_bulk_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(integer_bulk_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "integer_bulk_runner"],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    _build(generated, tmp_path)
    args = []
    for name in (["target"] if control else cases):
        entry = next(entry for entry in plan.id_map if entry["node"] == name and entry["key"] == "delays_i32")
        args.extend([name, str(entry["id"])])
    references = {}
    for mode in ("global", "task"):
        runner_mode = "control-" + mode if control else mode
        commands = [
            [str(ROOT / "build" / "tests" / "integer_bulk_runner.exe"), str(path),
             str(ROOT / "build" / "components"), runner_mode, *args],
            [str(generated / "build" / "integer_bulk_test.exe"), runner_mode, *args],
        ]
        for command in commands:
            for name in cases:
                (tmp_path / (name + ".wav")).unlink(missing_ok=True)
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            for name, params in cases.items():
                output = tmp_path / (name + ".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count, maximum = params["channels"], params["max_delay_samples"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames * count}h", stream.readframes(frames))
                if control and name != "target":
                    continue
                initial = [int(value) for value in params.get("initial_delays_i32", "").split(",")
                           if value]
                initial = initial or [0] * count
                vectors = ([([value] * count) for value in (0, 3, 3, 3, 1)] if control else [
                    initial, [(c*7+1) % (maximum+1) for c in range(count)],
                    [maximum - c % (maximum+1) for c in range(count)],
                    [c % 3 for c in range(count)], [2] * count,
                    [(c+1) % (maximum+1) for c in range(count)],
                ])
                expected, cursor = [], 0
                for blocks, vector in zip([1] * 5 if control else BLOCKS, vectors):
                    for n in range(cursor, cursor + blocks * 32):
                        for c, delay in enumerate(vector):
                            sample = samples[n-delay][c] if n >= delay else 0
                            expected.append(int(sample * 32767 / 32768))
                    cursor += blocks * 32
                assert max(abs(a-b) for a, b in zip(actual, expected)) <= 1, (mode, name)


def test_integer_bulk_delay_updates_four_entries(tmp_path):
    verify_integer_bulk_delay_cases(tmp_path, {
        "speaker": {"channels": 22, "max_delay_samples": 1440, "delay_vector_mode": 1,
                    "initial_delays_i32": ",".join(str(c*17) for c in range(22))},
        "phase": {"channels": 22, "max_delay_samples": 48, "delay_vector_mode": 1,
                  "initial_delays_i32": ",".join(str(c*2) for c in range(22))},
        "mono": {"channels": 1, "max_delay_samples": 4, "delay_vector_mode": 1,
                 "initial_delays_i32": "3"},
    })


def test_integer_bulk_control_range_errors_and_recovery_four_entries(tmp_path):
    verify_integer_bulk_delay_cases(tmp_path, {
        "control_source": {"channels": 3, "max_delay_samples": 12, "delay_vector_mode": 1,
                           "initial_delays_i32": "3,3,3"},
        "target": {"channels": 3, "max_delay_samples": 6, "delay_vector_mode": 1},
    }, control=True)
