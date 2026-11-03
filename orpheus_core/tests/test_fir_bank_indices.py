"""整数bank选择与浮点系数同块提交，保留输入历史的四入口参考验收。"""
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
BLOCKS = [8, 8, 1, 2, 1]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def bank_reference(samples, params):
    channels, taps, banks = params["channels"], params["num_taps"], params["num_filters"]
    coefs = [f32(float(v)) for v in params["coefs"].split(",")]
    initial = [int(v) for v in params.get("filter_indices", "").split(",")] if params.get("filter_indices") else [0]*channels
    history = [[] for _ in range(channels)]
    result, cursor = [], 0
    for step, blocks in enumerate(BLOCKS):
        scale = 0.5 if step % 2 else 1.0
        for row in samples[cursor:cursor+32*blocks]:
            for c in range(channels):
                history[c].insert(0, f32(row[c]/32768))
                del history[c][taps:]
                start = ((initial[c]+step) % banks)*taps
                value = 0.0
                for k in range(len(history[c])-1, -1, -1):
                    value = f32(value+f32(history[c][k]*f32(coefs[start+k]*scale)))
                result.append(int(value*32767))
        cursor += 32*blocks
    return result


def verify_banked_fir(tmp_path, cases):
    from test_generated_double_bank import _build

    frames, width = sum(BLOCKS)*32, max(p["channels"] for p in cases.values())
    samples = [[((n*97+c*577) % 12001)-6000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "FIR活动系数集验收"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": count,
                                            "indices": ",".join(map(str, range(count)))})
        nodes[name] = Node(id=name, component="orpheus.builtin.fir_bank", params=params)
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out",
                                 params={"channels": count, "file_path": name+".wav"})
        for left, right in (("source", name+"_route"), (name+"_route", name), (name, name+"_out")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry(); registry.add_search_path(ROOT / "components"); registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/bank_test.c").write_bytes((ROOT / "tests/fir_bank_indices_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(bank_test src/bank_test.c)\n"
                     "target_compile_definitions(bank_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(bank_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(
        ["cmake", "--build", str(ROOT / "build"), "--target", "orpheus_builtin_fir_bank",
         "orpheus_builtin_channel_router", "orpheus_builtin_wav_in", "orpheus_builtin_wav_out",
         "fir_bank_indices_runner"], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args = []
    for name, params in cases.items():
        args.append(name)
        for key, count, value_type in (
            ("filter_indices_i32", params["channels"], "int"),
            ("coefs_banked", params["num_filters"]*params["num_taps"], "float"),
        ):
            entry = next(e for e in plan.id_map if e["node"] == name and e["key"] == key)
            assert entry["count"] == count and entry["type"] == value_type
            args.append(str(entry["id"]))
    references = {}
    for mode in ("global", "task"):
        for command in (
            [str(ROOT / "build/tests/fir_bank_indices_runner.exe"), str(plan_path),
             str(ROOT / "build/components"), mode, *args],
            [str(generated / "build/bank_test.exe"), mode, *args],
        ):
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
                expected = bank_reference(samples, params)
                assert max(abs(a-b) for a, b in zip(actual, expected)) <= 1, (mode, name)


def test_fir_bank_indices_four_entries(tmp_path):
    taps = 442
    coefs = [[0.0]*taps for _ in range(4)]
    for c, bank in enumerate(coefs):
        bank[0], bank[1], bank[-1] = (c+1)/8, -0.125, (c+1)/32
    verify_banked_fir(tmp_path, {
        "banks": {"channels": 4, "num_taps": taps, "num_filters": 4, "coefs_mode": "banked",
                  "filter_indices": "0,1,2,3", "coefs": ",".join(str(v) for bank in coefs for v in bank)},
    })
