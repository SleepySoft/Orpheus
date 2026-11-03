"""双参数组反馈限幅：四入口、双bank热更新、旁路冻结与活动位图。"""
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
from test_phased_tone import f32

ROOT = Path(__file__).resolve().parents[2]
BLOCKS = [2, 1, 1, 2, 1, 1]
KEYS = ["feedback_attack", "feedback_release", "feedback_k1", "feedback_min_factor"]


def updated_parameters(channels):
    bases, factors, moduli = [.01, 1.003, .8, .2], [.005, .001, .1, .05], [3, 3, 5, 3]
    return {key: [f32(f32(bases[k])+f32((i % moduli[k])*f32(factors[k]))) for i in range(2*channels)]
            for k, key in enumerate(KEYS)}


def reference_block(samples, params, state):
    channels = params["channels"]
    values = {key: [f32(float(v)) for v in params[key].split(",")] for key in KEYS}
    if state["disabled"]:
        state["mask"] = 0
        return [int(f32(sample/32768)*32767) for row in samples for sample in row]
    output = [0]*(len(samples)*channels)
    mask = 0
    for c in range(channels):
        i = (state["parameter_set"]-1)*channels+c
        attack, release, k1, minimum = [values[key][i] for key in KEYS]
        gain, total_in, total_out = state["gains"][c], 0., 0.
        for n, row in enumerate(samples):
            x = f32(row[c]/32768)
            magnitude = abs(x)
            total_in = f32(total_in+magnitude)
            gain = min(f32(max(f32(f32(attack+1)-max(f32(f32(k1*gain)*magnitude), attack)), minimum)*f32(gain*release)), 1.)
            y = f32(x*gain)
            total_out = f32(total_out+abs(y))
            output[n*channels+c] = int(max(-1, min(1, y))*32767)
        state["gains"][c] = gain
        if total_out < total_in:
            mask |= 1 << c
    state["mask"] = mask if mask < (1 << 31) else mask-(1 << 32)
    state["gain"] = state["gains"][-1]
    return output


def verify_feedback_limiters(tmp_path, cases, reference=None):
    from test_generated_double_bank import _build
    frames, width = sum(BLOCKS)*32, max(p["channels"] for p in cases.values())
    samples = [[((n*113+c*911) % 50001)-25000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in", params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": count, "indices": ",".join(map(str, range(count)))})
        nodes[name] = Node(id=name, component="orpheus.builtin.limiter", params=params)
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out", params={"channels": count, "file_path": name+".wav"})
        for left, right in (("source", name+"_route"), (name+"_route", name), (name, name+"_out")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
    project = Project(metadata={"name": "双组反馈增益限幅"}, tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry(); registry.add_search_path(ROOT / "components"); registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/limiter_test.c").write_bytes((ROOT / "tests/feedback_limiter_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(limiter_test src/limiter_test.c)\n"
                     "target_compile_definitions(limiter_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(limiter_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "feedback_limiter_runner",
                             *["orpheus_builtin_"+c for c in ("limiter", "channel_router", "wav_in", "wav_out")]],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args = []
    for name, params in cases.items():
        args.append(name)
        for key in ("parameter_set", "disabled", *KEYS):
            slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == key)
            assert slot["kind"] == ("TUNE" if key in KEYS else "RTC")
            if key in KEYS:
                assert slot["form"] == "BULK" and slot["count"] == 2*params["channels"] and slot["type"] == "float"
            args.append(str(slot["id"]))
        assert next(e for e in plan.id_map if e["node"] == name and e["key"] == "active_mask")["kind"] == "PROBE"
    expected = {name: [] for name in cases}
    expected_states = bytearray()
    states = {name: {"gains": [1.]*p["channels"], "parameter_set": p.get("parameter_set", 1),
                     "disabled": p.get("disabled", False), "mask": 0, "gain": 1.} for name, p in cases.items()}
    block = 0
    for step, count in enumerate(BLOCKS):
        for state in states.values():
            if step in (1, 4):
                state["parameter_set"] = 2
            if step == 2:
                state["disabled"] = True
            if step in (3, 5):
                state.update(parameter_set=1, disabled=step == 5)
        for _ in range(count):
            for name, params in cases.items():
                if step >= 3:
                    params = dict(params, **{key: ",".join(format(v, ".9g") for v in values)
                                              for key, values in updated_parameters(params["channels"]).items()})
                chunk = [row[:params["channels"]] for row in samples[block*32:(block+1)*32]]
                state = states[name]
                expected[name] += (reference_block(chunk, params, state) if reference is None else reference(name, chunk, params, state))
                expected_states += struct.pack("<if", state["mask"], state["gain"])
            block += 1
    references = {}
    for mode in ("global", "task"):
        commands = [[str(ROOT / "build/tests/feedback_limiter_runner.exe"), str(plan_path), str(ROOT / "build/components"), mode, *args],
                    [str(generated / "build/limiter_test.exe"), mode, *args]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            assert (tmp_path / "limiter_states.bin").read_bytes() == expected_states
            for name, params in cases.items():
                output = tmp_path / (name+".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                assert max(abs(a-b) for a, b in zip(actual, expected[name])) <= 1, (mode, name)


def test_feedback_limiter_four_entries(tmp_path):
    cases = {}
    for channels in (3, 32):
        params = {"channels": channels, "mode": "feedback"}
        for key, values in zip(KEYS, ((.02, .05), (1.01, 1.002), (1.75, 3), (.25, .15))):
            params[key] = ",".join(str(v) for v in values for _ in range(channels))
        cases["limiter"+str(channels)] = params
    verify_feedback_limiters(tmp_path, cases)
