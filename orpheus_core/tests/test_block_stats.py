"""块级双dB统计输出为同周期非音频标量，不随音频块长扩张。"""
from dataclasses import asdict
import json
import math
from pathlib import Path
import shutil
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
FIXTURE = Path(__file__).parent / "fixtures/data_ports"


def verify_block_stats(tmp_path, cases, reference=None, trackers=None, tracker_reference=None,
                       volume_gains=None, gain_reference=None):
    from test_generated_double_bank import _build
    frames, width = 192, max(cases.values())
    samples = [[((n*113+c*911) % 50001)-25000 for c in range(width)] for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width); stream.setsampwidth(2); stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in", params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, channels in cases.items():
        nodes[name+"_route"] = Node(id=name+"_route", component="orpheus.builtin.channel_router",
                                    params={"channels_in": width, "channels_out": channels, "indices": ",".join(map(str, range(channels)))})
        nodes[name] = Node(id=name, component="orpheus.builtin.level_detect", params={"channels": channels, "execution_mode": 1})
        nodes[name+"_out"] = Node(id=name+"_out", component="orpheus.builtin.wav_out", params={"channels": channels, "file_path": name+".wav"})
        for left, right in (("source", name+"_route"), (name+"_route", name), (name, name+"_out")):
            edges.append(Connection(from_ref=PortRef(left, "out"), to_ref=PortRef(right, "in")))
        for port in ("rms", "peak"):
            nodes[name+"_"+port] = Node(id=name+"_"+port, component="orpheus.test.data_probe", params={"mode": 1})
            edges.append(Connection(from_ref=PortRef(name, port+"0"), to_ref=PortRef(name+"_"+port, "scalar")))
        if trackers is not None:
            nodes[name+"_track"] = Node(id=name+"_track", component="orpheus.builtin.level_tracker", params=trackers[name])
            for port in ("rms", "peak"):
                edges.append(Connection(from_ref=PortRef(name, port+"0"), to_ref=PortRef(name+"_track", port)))
            for port in ("level", "envelope"):
                nodes[name+"_"+port] = Node(id=name+"_"+port, component="orpheus.test.data_probe", params={"mode": 1})
                edges.append(Connection(from_ref=PortRef(name+"_track", port), to_ref=PortRef(name+"_"+port, "scalar")))
        if volume_gains is not None:
            nodes[name+"_gain"] = Node(id=name+"_gain", component="orpheus.builtin.gain_ramper", params=volume_gains[name])
            nodes[name+"_gain_out"] = Node(id=name+"_gain_out", component="orpheus.builtin.wav_out",
                                           params={"channels": channels, "file_path": name+"_gain.wav"})
            nodes[name+"_observed"] = Node(id=name+"_observed", component="orpheus.test.data_probe", params={"mode": 1})
            for a, x, b, y in ((name+"_route", "out", name+"_gain", "in"),
                               (name+"_gain", "out", name+"_gain_out", "in"),
                               (name+"_gain", "gain0", name+"_track", "volume_gain"),
                               (name+"_gain", "gain0", name+"_observed", "scalar")):
                edges.append(Connection(from_ref=PortRef(a, x), to_ref=PortRef(b, y)))
    project = Project(metadata={"name": "块级双dB非音频统计"}, tasks={"default": Task(id="default", sample_rate=48000, block_size=32)})
    project.graph = Graph(nodes=nodes, connections=edges)
    registry = Registry(); registry.add_search_path(ROOT / "components"); registry.scan()
    registry.add_manifest(FIXTURE / "component.yaml")
    plan = GraphCompiler(registry).compile(project)
    assert not plan.warnings
    if trackers is None and volume_gains is None:
        plan.entry_nodes = ["source"]
        plan.call_graph = {node: [] for node in plan.node_configs}
        for connection in edges:
            source, target = connection.from_ref, connection.to_ref
            config = plan.node_configs[source.node_id]
            plan.call_graph[source.node_id].append({
                "node": target.node_id, "every": 1, "phase": 0,
                "output_port": config.get("output_drive_ports", {}).get(source.port_id, 0),
                "input_port": plan.node_configs[target.node_id]["input_ports"].index(target.port_id),
            })
        for task in plan.tasks:
            task["entry_nodes"] = ["source"]
    else:
        use_linear_call_chain(plan)
    assert plan.schedule["tick"] == 32
    for name in cases:
        for port in ("rms", "peak"):
            buffer = next(b for b in plan.buffers.values() if b["to"] == name+"_"+port+":scalar")
            assert (buffer["type"], buffer["channels"], buffer["frame_count"]) == ("control", 1, 1)
            assert plan.execution_order.index(name) < plan.execution_order.index(name+"_"+port)
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/block_stats_test.c").write_bytes((ROOT / "tests/block_stats_runner.cpp").read_bytes())
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(block_stats_test src/block_stats_test.c)\n"
                     "target_compile_definitions(block_stats_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
                     "target_link_libraries(block_stats_test PRIVATE orpheus_graph)\n"
                     f'add_library(data_probe_dynamic SHARED "{(FIXTURE / "src/data_probe.c").as_posix()}")\n'
                     f'target_include_directories(data_probe_dynamic PRIVATE "{(FIXTURE / "include").as_posix()}")\n'
                     'set_target_properties(data_probe_dynamic PROPERTIES PREFIX "lib" OUTPUT_NAME "orpheus_test_data_probe" '
                     'WINDOWS_EXPORT_ALL_SYMBOLS ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
                     'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")\n')
    components = ("level_detect", "channel_router", "wav_in", "wav_out")
    if trackers is not None:
        components += ("level_tracker",)
    if volume_gains is not None:
        components += ("gain_ramper",)
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "block_stats_runner",
                             *["orpheus_builtin_"+c for c in components]], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    _build(generated, tmp_path)
    libraries = generated / "build/components"
    for component in components:
        for library in (ROOT / "build/components").glob("*orpheus_builtin_"+component+".*"):
            if library.suffix in (".dll", ".so", ".dylib"):
                shutil.copy2(library, libraries / library.name)
    expected, states, gain_states, gain_audio = [], {}, {}, {name: [] for name in cases}
    for block in range(6):
        chunk = samples[block*32:(block+1)*32]
        for name, channels in cases.items():
            rows = [[f32(v/32768) for v in row[:channels]] for row in chunk]
            if reference is not None:
                rms, peak = reference(rows)
            else:
                powers = []
                for c in range(channels):
                    power = f32(rows[0][c]*rows[0][c])
                    for row in rows[1:]:
                        power = f32(power+f32(row[c]*row[c]))
                    powers.append(f32(power/32))
                epsilon = f32(2.22044605e-16)
                rms, peak = [f32(f32(math.log10(f32(max(powers)+epsilon)))*10),
                             f32(f32(math.log10(f32(max(abs(v) for row in rows for v in row)+epsilon)))*20)]
            expected += [rms, peak]
            linear_gain = None
            if volume_gains is not None:
                g = volume_gains[name]
                if gain_reference is not None:
                    linear_gain, audio = gain_reference(name, rows, g, block)
                else:
                    current, remaining = gain_states.get(name, (f32(float(g["current_gains"])), int(g["remaining_blocks"])))
                    coefficient = 1.0
                    if not g.get("freeze", False):
                        if remaining == 0:
                            current = f32(float(g["target_gains"]))
                        else:
                            coefficient = f32(float(g["coefficients"])); remaining -= 1
                    audio = []
                    for row in rows:
                        current = f32(current*coefficient)
                        audio.extend(f32(v*current) for v in row)
                    gain_states[name] = current, remaining
                    linear_gain = current
                gain_audio[name] += [int(max(-1, min(1, v))*32767) for v in audio]
            if trackers is not None:
                if tracker_reference is not None:
                    if volume_gains is None:
                        expected += tracker_reference(name, trackers[name], block, rms, peak)
                    else:
                        expected += tracker_reference(name, trackers[name], block, rms, peak, linear_gain)
                    continue
                p = {k: f32(v) for k, v in trackers[name].items()}
                envelope, count, holding = states.get(name, (-abs(p["min_db"]), 0, True))
                level = 0.0
                if block != 3:
                    value = peak if block == 1 else rms
                    delta = f32(envelope-value)
                    if delta <= 0:
                        envelope, count, holding = value, 0, True
                    else:
                        coefficient = p["slow_decay_db"]
                        if delta >= p["hold_margin_db"]:
                            count += 1
                            if count > p["hold_calls"]:
                                holding = False
                        else:
                            count = 0
                        if not holding:
                            coefficient = p["fast_decay_db"]
                        if (1.0 if block == 5 else .5) > p["trigger_threshold"]:
                            coefficient = f32(p["fast_multiplier"]*p["fast_decay_db"])
                        envelope = f32(envelope+coefficient)
                    envelope = min(p["max_db"], max(p["min_db"], envelope))
                    volume_db = block*2 if linear_gain is None else f32(f32(math.log10(f32(linear_gain+f32(2.22044605e-16))))*20)
                    level = max(0.0, f32(f32(envelope+p["prediction_db_spl"])+volume_db))
                states[name] = envelope, count, holding
                expected += [level, envelope]
    baseline = None
    for mode in ("global", "task"):
        selected = (["--volume"] if volume_gains is not None else ["--track"] if trackers is not None else [])+list(cases)
        commands = [[str(ROOT / "build/tests/block_stats_runner.exe"), str(plan_path), str(libraries), mode, *selected],
                    [str(generated / "build/block_stats_test.exe"), mode, *selected]]
        for command in commands:
            result = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout+result.stderr
            actual = (tmp_path / "block_stats.bin").read_bytes()
            if baseline is None:
                baseline = actual
            assert actual == baseline
            values = struct.unpack(f"<{len(expected)}f", actual)
            if reference is None:
                assert max(abs(a-b) for a, b in zip(values, expected)) < 0.00002
            else:
                assert actual == struct.pack(f"<{len(expected)}f", *expected)
            for name, channels in cases.items():
                with wave.open(str(tmp_path / (name+".wav")), "rb") as stream:
                    actual_pcm = struct.unpack(f"<{frames*channels}h", stream.readframes(frames))
                expected_pcm = [int(f32(v/32768)*32767) for row in samples for v in row[:channels]]
                assert actual_pcm == tuple(expected_pcm)
                if volume_gains is not None:
                    with wave.open(str(tmp_path / (name+"_gain.wav")), "rb") as stream:
                        actual_pcm = struct.unpack(f"<{frames*channels}h", stream.readframes(frames))
                    assert actual_pcm == tuple(gain_audio[name])


def test_block_stats_four_entries(tmp_path):
    verify_block_stats(tmp_path, {"mono": 1, "multi": 10, "wide": 32})
