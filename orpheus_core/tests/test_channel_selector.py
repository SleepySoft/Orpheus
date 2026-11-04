"""双源通道选择、条件乘法和整数/浮点BULK的四入口验证。"""
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
from array_graph_runner import use_linear_call_chain

ROOT = Path(__file__).resolve().parents[2]
BLOCKS = [2, 1, 2, 1]


def verify_channel_selectors(tmp_path, cases):
    from test_generated_double_bank import _build

    frames = sum(BLOCKS)*32
    widths = {side: max(p["channels_"+side] for p in cases.values()) for side in ("a", "b")}
    samples = {
        side: [[((n*97+(c+(64 if side == "b" else 0))*577) % 12001)-6000
                for c in range(widths[side])] for n in range(frames)]
        for side in ("a", "b")
    }
    nodes, edges = {}, []
    for side in ("a", "b"):
        width = widths[side]
        with wave.open(str(tmp_path / (side+".wav")), "wb") as stream:
            stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
            stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples[side]))
        nodes["source_"+side] = Node(id="source_"+side, component="orpheus.builtin.wav_in",
                                    params={"channels": width, "file_path": side+".wav"})
    for name, params in cases.items():
        nodes[name] = Node(id=name, component="orpheus.builtin.channel_selector",
                           params={k: v for k, v in params.items() if not k.startswith("_")})
        for side in (("a", "b", "base") if params.get("accumulate") else ("a", "b")):
            if not params.get("_connect_"+side, True):
                continue
            route = name+"_"+side
            source = "a" if side == "base" else side
            count = params["channels_out"] if side == "base" else params["channels_"+side]
            nodes[route] = Node(id=route, component="orpheus.builtin.channel_router",
                                params={"channels_in": widths[source], "channels_out": count,
                                        "indices": ",".join(map(str, range(count)))})
            edges += [
                Connection(from_ref=PortRef("source_"+source, "out"), to_ref=PortRef(route, "in")),
                Connection(from_ref=PortRef(route, "out"), to_ref=PortRef(name, side)),
            ]
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out",
                                  params={"channels": params["channels_out"], "file_path": name+".wav"})
        edges.append(Connection(from_ref=PortRef(name, "out"), to_ref=PortRef(name+"_out", "in")))
    project = Project(metadata={"name": "双源运行期选路验收"},
                      tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry(); registry.add_search_path(ROOT / "components"); registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    use_linear_call_chain(plan)
    path = tmp_path / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/selector_test.c").write_bytes((ROOT / "tests/channel_selector_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(selector_test src/selector_test.c)\n"
                     "target_compile_definitions(selector_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(selector_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                             "orpheus_builtin_channel_selector", "channel_selector_runner",
                             "orpheus_builtin_channel_router", "orpheus_builtin_wav_in", "orpheus_builtin_wav_out"],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args, expected = [], {}
    for name, params in cases.items():
        count, base = params["channels_out"], params.get("index_base", 0)
        for key, value_type in (("indices", "int"), ("gains", "float")):
            slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == key)
            assert slot["form"] == "BULK" and slot["count"] == count and slot["type"] == value_type
            if key == "indices":
                args.append(name)
            args.append(str(slot["id"]))
        initial = [int(v) for v in params["indices"].split(",")] if params.get("indices") else list(range(base, base+count))
        gains = [float(v) for v in params["gains"].split(",")] if params.get("gains") else [1.0]*count
        reference, cursor = [], 0
        for step, blocks in enumerate(BLOCKS):
            indices = [v if v == 2147483647 else v+1 for v in initial] if step == 1 else [-1]*count if step == 2 else initial
            vector = [(c % 5-2)/4 for c in range(count)] if step == 1 else [0]*count if step == 2 else gains
            for n in range(cursor, cursor+blocks*32):
                for c, selected in enumerate(indices):
                    base_value = samples["a"][n][c]/32768 if params.get("accumulate") and params.get("_connect_base", True) else 0
                    index = selected-base
                    total = params["channels_a"]+params["channels_b"]
                    if index >= total and params.get("high_index_policy", 0):
                        index = total-1
                    if index < 0 or index >= total or (step == 2 and params.get("accumulate")):
                        reference.append(int(base_value*32767))
                        continue
                    side = "a" if index < params["channels_a"] else "b"
                    if not params.get("_connect_"+side, True):
                        reference.append(int(base_value*32767))
                        continue
                    channel = index if side == "a" else index-params["channels_a"]
                    value = samples[side][n][channel]/32768
                    if params.get("apply_gain", False):
                        value *= vector[c]
                    if params.get("accumulate"):
                        value = base_value+value
                    reference.append(int(value*32767))
            cursor += blocks*32
        expected[name] = reference
    references = {}
    for mode in ("global", "task"):
        commands = [
            [str(ROOT / "build/tests/channel_selector_runner.exe"), str(path), str(ROOT / "build/components"), mode, *args],
            [str(generated / "build/selector_test.exe"), mode, *args],
        ]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            for name, params in cases.items():
                output = tmp_path / (name+".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels_out"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                assert max(abs(a-b) for a, b in zip(actual, expected[name])) <= 1, (mode, name)


def test_channel_selector_four_entries(tmp_path):
    mapping = [-2147483648, -1, 0, 1, 21, 22, 23, 70, 71, 72, 2147483647]
    verify_channel_selectors(tmp_path, {
        "main": {"channels_a": 22, "channels_b": 49, "channels_out": 22, "index_base": 1,
                 "high_index_policy": 1, "apply_gain": True, "indices": ",".join(map(str, mapping*2)),
                 "gains": ",".join(str((c % 7-3)/4) for c in range(22))},
        "copy": {"channels_a": 64, "channels_b": 64, "channels_out": 64, "index_base": 0,
                 "indices": ",".join(str(v) for v in ([0, 63, 64, 127, 128, -1, 2147483647, -2147483648]*8))},
        "missing": {"channels_a": 17, "channels_b": 32, "channels_out": 6, "index_base": 1,
                    "indices": "0,1,17,18,49,50", "apply_gain": True, "_connect_b": False},
        "add": {"channels_a": 17, "channels_b": 1, "channels_out": 22, "index_base": 1,
                "indices": ",".join(map(str, mapping*2)), "apply_gain": True, "accumulate": True, "_connect_b": False},
    })
