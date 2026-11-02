"""公共整数延迟：真实参数调用、历史连续性和四入口独立数值验收。"""
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
BLOCKS = [160, 1, 7, 5, 4, 8]
DELAYS = [None, 0, "max", 1, 3, -1]


def verify_integer_delay_cases(tmp_path, cases):
    from test_generated_double_bank import _build

    frames = sum(BLOCKS) * 32
    width = max(p["channels"] for p in cases.values())
    samples = [[((n * 97 + c * 577) % 12001) - 6000 for c in range(width)]
               for n in range(frames)]
    with wave.open(str(tmp_path / "input.wav"), "wb") as stream:
        stream.setnchannels(width)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack(f"<{width}h", *row) for row in samples))
    project = Project(metadata={"name": "运行期整数延迟"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    nodes = {"source": Node(id="source", component="orpheus.builtin.wav_in",
                           params={"channels": width, "file_path": "input.wav"})}
    edges = []
    for name, params in cases.items():
        count = params["channels"]
        nodes[name + "_route"] = Node(id=name + "_route", component="orpheus.builtin.channel_router",
                                     params={"channels_in": width, "channels_out": count,
                                             "indices": ",".join(str(c) for c in range(count))})
        nodes[name] = Node(id=name, component="orpheus.builtin.delay_line", params=params)
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
    (generated / "src/delay_test.c").write_text(r'''
#include "orpheus_graph.h"
#include "orpheus_control.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char** argv) {
    if (argc < 3) return 1;
    if (orpheus_generated_init(48000, 32) != ORPHEUS_OK) return 1;
    for (int i = 2; i < argc; ++i) {
        OrpheusValue channels;
        if (orpheus_control_get_value(argv[i], "channels", &channels) != ORPHEUS_OK) return 1;
        if (orpheus_control_bulk_count(argv[i], "delays_samples") != (size_t)channels.value.i32) {
            fprintf(stderr, "BULK通道数未初始化: %s\n", argv[i]); return 1;
        }
    }
    const unsigned blocks[] = {160, 1, 7, 5, 4, 8};
    const int32_t delays[] = {0, 0, 6, 1, 3, -1};
    for (unsigned step = 0; step < 6; ++step) {
        if (step) {
            OrpheusValue value = {.type=ORPHEUS_VALUE_INT, .value.i32=delays[step]};
            for (int i = 2; i < argc; ++i) {
                OrpheusValue maximum;
                if (orpheus_control_get_value(argv[i], "max_delay_samples", &maximum) != ORPHEUS_OK) return 1;
                value.value.i32 = step == 2 ? maximum.value.i32 : delays[step];
                if (value.value.i32 > maximum.value.i32) value.value.i32 = maximum.value.i32;
                int rc = orpheus_control_set_value(argv[i], "delay_samples", value);
                if (rc != ORPHEUS_OK) { fprintf(stderr, "SET: %s %d\n", argv[i], rc); return 1; }
            }
        }
        if (step == 4) {
            for (int i = 2; i < argc; ++i) {
                OrpheusValue channels;
                float delays[64];
                if (orpheus_control_get_value(argv[i], "channels", &channels) != ORPHEUS_OK) return 1;
                for (int c = 0; c < channels.value.i32; ++c) delays[c] = (float)(c % 2 + 1);
                if (orpheus_control_write_bulk(argv[i], "delays_samples", delays,
                                              (size_t)channels.value.i32) != ORPHEUS_OK) return 1;
            }
        }
        for (unsigned i = 0; i < blocks[step]; ++i) {
            int rc = strcmp(argv[1], "task") == 0
                ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32);
            if (rc != ORPHEUS_OK) { fprintf(stderr, "执行错误 %d\n", rc); return 1; }
        }
    }
    orpheus_generated_teardown();
    return 0;
}
''', encoding="utf-8")
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(delay_test src/delay_test.c)\n"
                     "target_link_libraries(delay_test PRIVATE orpheus_graph)\n")
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                             "integer_delay_runner"], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    _build(generated, tmp_path)
    references = {}
    for mode in ("global", "task"):
        commands = [
            [str(ROOT / "build/tests/integer_delay_runner.exe"), str(path),
             str(ROOT / "build/components"), mode, *cases],
            [str(generated / "build/delay_test.exe"), mode, *cases],
        ]
        for command in commands:
            for name in cases:
                (tmp_path / (name + ".wav")).unlink(missing_ok=True)
            result = subprocess.run(command, cwd=tmp_path, capture_output=True,
                                    text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            for name, params in cases.items():
                output = tmp_path / (name + ".wav")
                data = output.read_bytes()
                assert data == references.setdefault(name, data), (mode, name)
                count = params["channels"]
                with wave.open(str(output), "rb") as stream:
                    assert (stream.getnframes(), stream.getnchannels(), stream.getframerate()) == (
                        frames, count, 48000,
                    )
                    actual = struct.unpack(f"<{frames * count}h", stream.readframes(frames))
                legacy = [float(v) for v in params.get("delays_samples", "0").split(",")]
                legacy.extend([0] * (count - len(legacy)))
                expected, cursor = [], 0
                for blocks, update in zip(BLOCKS, DELAYS):
                    delay = params.get("delay_samples", -1) if update is None else (
                        params["max_delay_samples"] if update == "max" else
                        min(update, params["max_delay_samples"])
                    )
                    for n in range(cursor, cursor + blocks * 32):
                        for c in range(count):
                            d = delay if delay >= 0 else (
                                c % 2 + 1 if update == -1 else int(legacy[c] + 0.5)
                            )
                            sample = samples[n - d][c] if n >= d else 0
                            expected.append(int(sample * 32767 / 32768))
                    cursor += blocks * 32
                assert max(abs(a - b) for a, b in zip(actual, expected)) <= 1, name


def test_integer_delay_runtime_updates_four_entries(tmp_path):
    verify_integer_delay_cases(tmp_path, {
        "uniform": {"channels": 3, "max_delay_samples": 16, "delay_samples": 5,
                    "delays_samples": "1,3,7"},
        "legacy": {"channels": 2, "max_delay_samples": 16, "delays_samples": "2,6"},
    })
