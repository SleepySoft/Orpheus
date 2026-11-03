"""显式两阶段包络/滤波核：四入口PCM及阶段/计数/当前增益逐位参考。"""
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
STEPS = 11


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def update_reference(state, step):
    if step in (1, 9):
        state["phase"] = 0
        state["ramps"][0] = [f32(.9 if step == 1 else 1.4), f32(1.5 if step == 1 else .6),
                              f32(1.01 if step == 1 else .99), 1 if step == 1 else -2147483648]
        state["ramps"][1] = [f32(1.25), f32(.7), f32(.995), 2 if step == 1 else 0]
    if step in (2, 6):
        state["freeze"] = True
    if step in (3, 7):
        state["freeze"] = False
    if step == 6:
        state["phase"] = 1
        state["ramps"][1] = [f32(.8), f32(1.2), f32(1.01), 1]
    if step == 10:
        state["ramps"][0][3] = 0


def reference_block(samples, params, state):
    phase = state["phase"]
    ramp = state["ramps"][phase]
    if not state["freeze"] and ramp[3] == 0 and phase == 0 and state["ramps"][1][3] > 0:
        phase = state["phase"] = 1
        ramp = state["ramps"][phase]
    coefficient = 1.0
    gain = ramp[0]
    if not state["freeze"]:
        if ramp[3] == 0:
            gain = ramp[1]
        else:
            coefficient = ramp[2]
            ramp[3] = max(-2147483648, ramp[3]-1)
    coefs = [f32(float(v)) for v in params["cut_coefs" if gain < 1 else "boost_coefs"].split(",")]
    output = []
    for row in samples:
        gain = f32(gain*coefficient)
        for c, sample in enumerate(row):
            x = f32(sample/32768)
            filtered = f32(f32(x*coefs[0])+state["z1"][c])
            if params["filter_order"] == 1:
                state["z1"][c] = f32(f32(x*coefs[1])+f32(coefs[3]*filtered))
            else:
                first = state["z2"][c] if coefs[1] == 0 else f32(f32(x*coefs[1])+state["z2"][c])
                state["z1"][c] = f32(first-f32(coefs[3]*filtered))
                state["z2"][c] = f32(f32(x*coefs[2])-f32(coefs[4]*filtered))
            value = f32(f32(f32(gain-1)*filtered)+x)
            output.append(int(max(-1, min(1, value))*32767))
    ramp[0] = gain
    return output


def verify_phased_tones(tmp_path, cases, reference=None):
    from test_generated_double_bank import _build

    frames, width = STEPS*32, max(p["channels"] for p in cases.values())
    samples = [[((n*97+c*577) % 6001)-3000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in", params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": count, "indices": ",".join(map(str, range(count)))})
        nodes[name] = Node(id=name, component="orpheus.builtin.indexed_tone", params=params)
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out", params={"channels": count, "file_path": name+".wav"})
        for left, right in (("source", name+"_route"), (name+"_route", name), (name, name+"_out")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
    project = Project(metadata={"name": "显式两阶段音调核"}, tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry(); registry.add_search_path(ROOT / "components"); registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/tone_test.c").write_bytes((ROOT / "tests/phased_tone_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(tone_test src/tone_test.c)\n"
                     "target_compile_definitions(tone_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(tone_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "phased_tone_runner",
                             *["orpheus_builtin_"+c for c in ("indexed_tone", "channel_router", "wav_in", "wav_out")]],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args = []
    for name in cases:
        args.append(name)
        for key in ("phase", "ramp0_remaining_blocks", "ramp0_current_gain", "boost_coefs", "cut_coefs"):
            slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == key)
            assert slot["kind"] == ("TUNE" if key.endswith("_coefs") else "RTC")
            args.append(str(slot["id"]))
        for key in ("boost_coefs", "cut_coefs"):
            slot = next(e for e in plan.id_map if e["node"] == name and e["key"] == key)
            assert slot["form"] == "BULK" and slot["count"] == 5 and slot["type"] == "float"
    expected = {name: [] for name in cases}
    expected_states = bytearray()
    states = {name: {"phase": 0, "freeze": False, "ramps": [[1., 1., 1., 0], [1., 1., 1., 0]],
                     "z1": [0.]*p["channels"], "z2": [0.]*p["channels"]} for name, p in cases.items()}
    for step in range(STEPS):
        for name, params in cases.items():
            state = states[name]
            update_reference(state, step)
            if step >= 5:
                params = dict(params, boost_coefs=".6,0,-.2,.15,.025", cut_coefs=".2,0,0,-.1,.05")
            chunk = [row[:params["channels"]] for row in samples[step*32:(step+1)*32]]
            if reference is None:
                output = reference_block(chunk, params, state)
            else:
                output = reference(name, chunk, params, state)
            expected[name] += output
            expected_states += struct.pack("<iiiff", state["phase"]+1, state["ramps"][0][3],
                                           state["ramps"][1][3], state["ramps"][0][0], state["ramps"][1][0])
    references = {}
    for mode in ("global", "task"):
        commands = [[str(ROOT / "build/tests/phased_tone_runner.exe"), str(plan_path), str(ROOT / "build/components"), mode, *args],
                    [str(generated / "build/tone_test.exe"), mode, *args]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            assert (tmp_path / "tone_states.bin").read_bytes() == expected_states
            for name, params in cases.items():
                output = tmp_path / (name+".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, count)
                    actual = struct.unpack(f"<{frames*count}h", stream.readframes(frames))
                assert max(abs(a-b) for a, b in zip(actual, expected[name])) <= 1, (mode, name)


def test_phased_tone_four_entries(tmp_path):
    verify_phased_tones(tmp_path, {
        "first": {"channels": 3, "execution_mode": 1, "filter_order": 1,
                  "boost_coefs": ".5,.25,0,.25,0", "cut_coefs": ".25,.125,0,-.25,0"},
        "second": {"channels": 10, "execution_mode": 1, "filter_order": 2,
                   "boost_coefs": ".4,0,-.2,.1,.05", "cut_coefs": ".3,0,-.1,-.2,.1"},
        "second_dense": {"channels": 1, "execution_mode": 1, "filter_order": 2,
                         "boost_coefs": ".4,.15,-.2,.1,.05", "cut_coefs": ".3,.1,-.1,-.2,.1"},
    })
