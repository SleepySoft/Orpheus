"""奇偶序列锁存目标，保留严格IIR和有序密集矩阵乘，四入口对拍。"""
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
BLOCKS = [1, 1, 1, 2, 1, 1, 1, 1, 1]
SEQUENCES = [0, 0, 1, 2, 2, 4, -2147483648, -2147483647, 0]


def reference_matrix(samples, params, state):
    rows, cols = params["rows"], params["cols"]
    if not state["initialized"]:
        state["active_gains"] = list(state["requested_gains"])
        state["accepted_targets"] = list(state["requested_gains"])
        state["initialized"] = True
    frozen = state["sequence"] & 1
    if state["last_sequence"] != state["sequence"]:
        if not frozen:
            state["active_gains"] = [max(v, f32(5.0118723e-7)) for v in state["active_gains"]]
            state["accepted_targets"] = list(state["requested_gains"])
        state["last_sequence"] = state["sequence"]
    coefficient = f32(params["ramp_coeff"])
    other = f32(1-coefficient)
    output = []
    for row in samples:
        if not frozen:
            state["active_gains"] = [f32(f32(other*t)+f32(coefficient*g)) for g, t in
                                     zip(state["active_gains"], state["accepted_targets"])]
        for r in range(rows):
            total = 0.
            for c in range(cols):
                total = f32(total+f32(f32(row[c]/32768)*state["active_gains"][r*cols+c]))
            output.append(int(max(-1, min(1, total))*32767))
    return output


def verify_sequence_matrices(tmp_path, cases, reference=None):
    from test_generated_double_bank import _build
    frames, width = sum(BLOCKS)*32, max(p["cols"] for p in cases.values())
    samples = [[((n*113+c*911) % 50001)-25000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in", params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": params["cols"],
                                            "indices": ",".join(map(str, range(params["cols"])))})
        nodes[name] = Node(id=name, component="orpheus.builtin.slc_matrix_mul", params=params)
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out", params={"channels": params["rows"], "file_path": name+".wav"})
        for left, right in (("source", name+"_route"), (name+"_route", name), (name, name+"_out")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
    project = Project(metadata={"name": "序列驱动渐变矩阵"}, tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry(); registry.add_search_path(ROOT / "components"); registry.scan()
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/sequence_matrix_test.c").write_bytes((ROOT / "tests/sequence_matrix_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(sequence_matrix_test src/sequence_matrix_test.c)\n"
                     "target_compile_definitions(sequence_matrix_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(sequence_matrix_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "sequence_matrix_runner",
                             *["orpheus_builtin_"+c for c in ("slc_matrix_mul", "channel_router", "wav_in", "wav_out")]],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    args, states, expected = [], {}, {name: [] for name in cases}
    for name, params in cases.items():
        count = params["rows"]*params["cols"]
        sequence = next(e for e in plan.id_map if e["node"] == name and e["key"] == "sequence")
        requested = next(e for e in plan.id_map if e["node"] == name and e["key"] == "requested_gains")
        assert sequence["type"] == "int" and sequence["kind"] == "RTC"
        assert requested["type"] == "float" and requested["form"] == "BULK" and requested["count"] == count
        args += [name, str(sequence["id"]), str(requested["id"]), str(count)]
        requested_values = [f32(float(v)) for v in params["requested_gains"].split(",")]
        states[name] = {"requested_gains": requested_values, "initialized": False, "last_sequence": 0}
    expected_states = bytearray()
    block = 0
    for step, blocks in enumerate(BLOCKS):
        for name, state in states.items():
            state["sequence"] = SEQUENCES[step]
            if step:
                state["requested_gains"] = [f32(f32((k % 9-4)*f32(.03))+f32(step*f32(.002))) for k in range(len(state["requested_gains"]))]
        for _ in range(blocks):
            for name, params in cases.items():
                chunk = [row[:params["cols"]] for row in samples[block*32:(block+1)*32]]
                state = states[name]
                expected[name] += reference_matrix(chunk, params, state) if reference is None else reference(name, chunk, params, state)
                count = params["rows"]*params["cols"]
                for key in ("requested_gains", "active_gains", "accepted_targets"):
                    expected_states += struct.pack(f"<{count}f", *state[key])
                expected_states += struct.pack("<i", state["last_sequence"])
            block += 1
    references = {}
    for mode in ("global", "task"):
        commands = [[str(ROOT / "build/tests/sequence_matrix_runner.exe"), str(plan_path), str(ROOT / "build/components"), mode, *args],
                    [str(generated / "build/sequence_matrix_test.exe"), mode, *args]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            assert (tmp_path / "sequence_matrix_states.bin").read_bytes() == expected_states
            for name, params in cases.items():
                path = tmp_path / (name+".wav")
                data = path.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                with wave.open(str(path), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels()) == (frames, params["rows"])
                    actual = struct.unpack(f"<{frames*params['rows']}h", stream.readframes(frames))
                assert max(abs(a-b) for a, b in zip(actual, expected[name])) <= 1, (mode, name)


def test_sequence_matrix_four_entries(tmp_path):
    cases = {}
    for name, rows, cols, coefficient in (("small", 2, 3, .995), ("wide", 32, 32, .0000001), ("hold", 1, 1, 1.)):
        cases[name] = {"rows": rows, "cols": cols, "execution_mode": 1, "ramp_coeff": coefficient,
                       "requested_gains": ",".join(format((k % 7-3)*.07, ".9g") for k in range(rows*cols))}
    verify_sequence_matrices(tmp_path, cases)
