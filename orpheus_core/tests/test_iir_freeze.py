"""IIR暂停时输出正零且不推进状态；同一输入的四入口参考验收。"""
from dataclasses import asdict
from functools import partial
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


def iir_reference(samples, params, *, coefficient_updates=False):
    channels, stages = params["channels"], params["num_stages"]
    flat = [float(v) for v in params["coefs"].split(",")]
    per_channel = params.get("coefs_mode", "shared") == "per_channel"
    counts = ([int(v) for v in params["stages_per_channel"].split(",")]
              if params.get("stages_per_channel") else [stages]*channels)
    memory = [[[0.0, 0.0] for _ in range(stages)] for _ in range(channels)]
    expected, cursor = [], 0
    for step, blocks in enumerate(BLOCKS):
        for n in range(cursor, cursor+32*blocks):
            for c in range(channels):
                if step % 2 and not coefficient_updates:
                    expected.append(0)
                    continue
                x = samples[n][c] / 32768
                for stage in range(counts[c]):
                    start = ((c*stages if per_channel else 0)+stage)*5
                    b0, b1, b2, a1, a2 = flat[start:start+5]
                    if step % 2 and coefficient_updates:
                        b0 = b1 = b2 = a1 = a2 = 0.0
                    z1, z2 = memory[c][stage]
                    y = b0*x+z1
                    memory[c][stage] = [b1*x+z2-a1*y, b2*x-a2*y]
                    x = y
                expected.append(int(x*32767))
        cursor += 32*blocks
    return expected


def verify_iir_freeze_cases(tmp_path, cases, *, component="orpheus.builtin.iir_bank",
                            reference=iir_reference, bulk_stride=80, coefficient_updates=False):
    from test_generated_double_bank import _build

    frames, width = sum(BLOCKS)*32, max(params["channels"] for params in cases.values())
    samples = [[((n*97+c*577) % 12001)-6000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(flow_mode="task_sequence", metadata={"name": "IIR冻结验收"})
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
        nodes[name] = Node(id=name, component=component, params=params)
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
    for name, params in cases.items():
        if params.get("coefs_mode") == "per_channel":
            slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == "coefs_per_channel")
            assert slot["count"] == params["channels"]*bulk_stride
    path = tmp_path / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    runner = "iir_coefs_runner" if coefficient_updates else "filter_freeze_runner"
    (generated / "src/freeze_test.c").write_bytes((ROOT / "tests" / (runner + ".cpp")).read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(freeze_test src/freeze_test.c)\n"
                     "target_compile_definitions(freeze_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(freeze_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(
        ["cmake", "--build", str(ROOT / "build"), "--target", component.replace(".", "_"),
         "orpheus_builtin_channel_router", "orpheus_builtin_wav_in", "orpheus_builtin_wav_out",
         runner],
        capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    _build(generated, tmp_path)
    args = []
    for name, params in cases.items():
        if coefficient_updates:
            key = "coefs_per_channel" if params.get("coefs_mode") == "per_channel" else "coefs"
            entry = next(e for e in plan.id_map if e["node"] == name and e["key"] == key)
            assert entry["form"] == "BULK" and entry["count"] == (
                params["channels"] if key == "coefs_per_channel" else 1)*bulk_stride
            args += [name, str(entry["id"]), key, str(entry["count"])]
        else:
            entry = next(e for e in plan.id_map if e["node"] == name and e["key"] == "processing_enabled")
            assert entry["kind"] == "RTC" and entry["form"] == "SCALAR"
            args += [name, str(entry["id"])]
    references = {}
    for mode in ("global", "task"):
        commands = [
            [str(ROOT / "build/tests" / (runner + ".exe")), str(path),
             str(ROOT / "build/components"), mode, *args],
            [str(generated / "build/freeze_test.exe"), mode, *args],
        ]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            for name, params in cases.items():
                output = tmp_path / (name + ".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                expected = reference(samples, params)
                assert max(abs(a-b) for a, b in zip(actual, expected)) <= 1, (mode, name)


def test_iir_freeze_four_entries(tmp_path):
    verify_iir_freeze_cases(tmp_path, {
        "shared": {"channels": 3, "num_stages": 1, "coefs": "0.5,0.25,0,-0.5,0"},
        "separate": {"channels": 2, "num_stages": 2, "coefs_mode": "per_channel",
                     "coefs": "0.5,0.25,0,-0.5,0,1,0,0,0,0,0.25,0.5,0,-0.25,0,0.5,0,0,0,0"},
        "varying_shared": {"channels": 3, "num_stages": 3, "stages_per_channel": "0,1,3",
                           "coefs": "0.5,0.25,0,-0.5,0,0.5,0.125,0,-0.25,0,0.25,0.25,0,0,0"},
        "varying_separate": {"channels": 2, "num_stages": 2, "stages_per_channel": "1,2",
                             "coefs_mode": "per_channel",
                             "coefs": "0.5,0.25,0,-0.5,0,nan,nan,nan,nan,nan,0.25,0.5,0,-0.25,0,0.5,0,0,0,0"},
    })


def test_iir_bank_coefficient_swap_four_entries(tmp_path):
    rows = []
    for channel in range(64):
        rows.extend([.125 + (channel % 3) * .125, .0625, 0, -.25, 0, .5, .125, 0, -.25, 0])
    verify_iir_freeze_cases(tmp_path, {
        "shared": {"channels": 3, "num_stages": 2, "coefs": "0.5,0.25,0,-0.5,0,0.5,0,0,0,0"},
        "wide": {"channels": 64, "num_stages": 2, "coefs_mode": "per_channel",
                 "stages_per_channel": ",".join(str(c % 3) for c in range(64)),
                 "coefs": ",".join(map(str, rows))},
    }, coefficient_updates=True, reference=partial(iir_reference, coefficient_updates=True))
