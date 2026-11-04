"""显式逐样本分组包络：整数BULK计数、跨块吸附、冻结与四入口。"""
from dataclasses import asdict
import json
import math
from pathlib import Path
import struct
import subprocess
import wave

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry
from array_graph_runner import use_linear_call_chain
from test_phased_tone import f32

ROOT = Path(__file__).resolve().parents[2]
BLOCKS = [2, 1, 1, 2, 1, 1]
KEYS = ["current_gains", "target_gains", "coefficients", "remaining_blocks"]


def update_reference(state, step):
    count = len(state["current_gains"])
    if step in (1, 5):
        state["current_gains"] = [f32(.9) if step == 5 else f32(f32(.6)+f32((r % 3)*f32(.1))) for r in range(count)]
        state["target_gains"] = [f32(.4) if step == 5 else f32(f32(.2)+f32((r % 4)*f32(.05))) for r in range(count)]
        state["coefficients"] = [1. if step == 5 else f32(f32(.98)+f32((r % 5)*f32(.003))) for r in range(count)]
        state["remaining_blocks"] = [2147483647-r if step == 5 else 3 for r in range(count)]
    if step in (2, 3):
        state["freeze"] = step == 2


def reference_block(samples, params, state):
    count, channels = params["num_rampers"], params["channels"]
    mapping = [int(v) for v in params["chan_map"].split(",")] if params.get("chan_map") else [0]*channels
    gains, coefficients = list(state["current_gains"]), [1.]*count
    for r in range(count):
        if not state["freeze"]:
            if not state["remaining_blocks"][r]:
                gains[r] = state["target_gains"][r]
            else:
                state["remaining_blocks"][r] -= 1
                coefficients[r] = state["coefficients"][r]
    starts = list(gains)
    output = []
    for n, row in enumerate(samples):
        gains = ([f32(g*f32(math.pow(c, n+1))) for g, c in zip(starts, coefficients)]
                 if params["execution_mode"] == 2 else [f32(g*c) for g, c in zip(gains, coefficients)])
        for c, sample in enumerate(row):
            x = f32(sample/32768)
            y = 0. if mapping[c] == -2 else x if mapping[c] < 0 else f32(x*gains[mapping[c]])
            output.append(int(max(-1, min(1, y))*32767))
    state["current_gains"] = gains
    return output


def verify_sample_gains(tmp_path, cases, reference=None, component="gain_ramper", input_ports=("in",)):
    from test_generated_double_bank import _build
    frames, width = sum(BLOCKS)*32, max(p["channels"] for p in cases.values())*len(input_ports)
    samples = [[((n*113+c*911) % 50001)-25000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in", params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        node_params = params if component == "gain_ramper" else {k: v for k, v in params.items() if k != "num_rampers"}
        nodes[name] = Node(id=name, component="orpheus.builtin."+component, params=node_params)
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out", params={"channels": count, "file_path": name+".wav"})
        for port_index, port in enumerate(input_ports):
            route = name+"_route"+("" if port_index == 0 else "_"+port)
            nodes[route] = Node(id=route, component="orpheus.builtin.channel_router",
                                params={"channels_in": width, "channels_out": count,
                                        "indices": ",".join(str(c+port_index*count) for c in range(count))})
            edges.extend([Connection(from_ref=PortRef("source", "out"), to_ref=PortRef(route, "in")),
                          Connection(from_ref=PortRef(route, "out"), to_ref=PortRef(name, port))])
        edges.append(Connection(from_ref=PortRef(name, "out"), to_ref=PortRef(name+"_out", "in")))
    project = Project(metadata={"name": "显式逐样本分组增益"}, tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry(); registry.add_search_path(ROOT / "components"); registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    use_linear_call_chain(plan)
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/sample_gain_test.c").write_bytes((ROOT / "tests/sample_gain_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(sample_gain_test src/sample_gain_test.c)\n"
                     "target_compile_definitions(sample_gain_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(sample_gain_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "sample_gain_runner",
                             *["orpheus_builtin_"+c for c in (component, "channel_router", "wav_in", "wav_out")]],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args = []
    for name, params in cases.items():
        args.append(name)
        for key in ("freeze", *KEYS):
            slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == key)
            assert slot["kind"] == ("RTC" if key == "freeze" else "TUNE")
            if key != "freeze":
                assert slot["form"] == "BULK" and slot["count"] == params["num_rampers"]
                assert slot["type"] == ("int" if key == "remaining_blocks" else "float")
            args.append(str(slot["id"]))
        args.append(str(params["num_rampers"]))
    expected, states = {name: [] for name in cases}, {}
    for name, params in cases.items():
        state = {"freeze": params.get("freeze", False)}
        for key in KEYS:
            state[key] = [(int(v) if key == "remaining_blocks" else f32(float(v))) for v in params[key].split(",")] if params.get(key) else [
                0 if key == "remaining_blocks" else 1.]*params["num_rampers"]
        states[name] = state
    expected_states = bytearray()
    block = 0
    for step, count in enumerate(BLOCKS):
        for state in states.values():
            update_reference(state, step)
        for _ in range(count):
            for name, params in cases.items():
                chunk = [row[:params["channels"]*len(input_ports)] for row in samples[block*32:(block+1)*32]]
                state = states[name]
                expected[name] += (reference_block(chunk, params, state) if reference is None else reference(name, chunk, params, state))
                size = params["num_rampers"]
                expected_states += struct.pack(f"<{size}f", *state["current_gains"])
                expected_states += struct.pack(f"<{size}i", *state["remaining_blocks"])
            block += 1
    references = {}
    for mode in ("global", "task"):
        commands = [[str(ROOT / "build/tests/sample_gain_runner.exe"), str(plan_path), str(ROOT / "build/components"), mode, *args],
                    [str(generated / "build/sample_gain_test.exe"), mode, *args]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            assert (tmp_path / "sample_gain_states.bin").read_bytes() == expected_states
            for name, params in cases.items():
                output = tmp_path / (name+".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                assert max(abs(a-b) for a, b in zip(actual, expected[name])) <= 1, (mode, name)


def test_sample_gain_four_entries(tmp_path):
    verify_sample_gains(tmp_path, {
        "mapped": {"channels": 7, "execution_mode": 1, "num_rampers": 3, "chan_map": "0,1,-1,0,2,-1,2"},
        "wide": {"channels": 64, "execution_mode": 1, "num_rampers": 32, "chan_map": ",".join(str(c//2) for c in range(64))},
        "unused": {"channels": 3, "execution_mode": 1, "num_rampers": 4, "chan_map": "-1,-1,-1"},
        "power": {"channels": 7, "execution_mode": 2, "num_rampers": 4, "chan_map": "0,1,-1,2,-2,0,2"},
        "wide_power": {"channels": 64, "execution_mode": 2, "num_rampers": 4, "chan_map": ",".join(str(c%4) if c < 62 else "-2" for c in range(64))},
    })
