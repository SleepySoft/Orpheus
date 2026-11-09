"""真实宿主缓冲接口；缺块报错，不用置零代替输入。"""
from dataclasses import asdict
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project, ProjectLoader, Task
from orpheus_core.registry import Registry
from test_generated_double_bank import _build

ROOT = Path(__file__).resolve().parents[2]


def test_host_audio_native_contract(tmp_path):
    match = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", (ROOT / "build/CMakeCache.txt").read_text(encoding="utf-8"), re.M)
    assert match
    directory = ROOT / "components/orpheus/builtin/host_audio"
    executable = tmp_path / "host_audio_smoke.exe"
    result = subprocess.run([match[1], "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DORPHEUS_BUILDING_COMPONENT", "-I" + str(ROOT / "orpheus_abi/include"), "-I" + str(directory / "include"),
        str(directory / "src/host_audio.c"), str(ROOT / "tests/host_audio_smoke.c"), "-o", str(executable)],
        capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


def test_buffer_source_native_contract(tmp_path):
    match = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", (ROOT / "build/CMakeCache.txt").read_text(encoding="utf-8"), re.M)
    assert match
    directory = ROOT / "components/orpheus/builtin/buffer_source"
    executable = tmp_path / "buffer_source_smoke.exe"
    result = subprocess.run([match[1], "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DORPHEUS_BUILDING_COMPONENT", "-I" + str(ROOT / "orpheus_abi/include"),
        "-I" + str(directory / "include"),
        "-I" + str(ROOT / "components/orpheus/builtin/host_audio/include"),
        str(directory / "src/buffer_source.c"), str(ROOT / "tests/buffer_source_smoke.c"), "-o", str(executable)],
        capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("component,channels,frames", [
    ("host_audio", 3, 32), ("host_audio", 30, 96),
    ("buffer_source", 3, 32), ("buffer_source", 30, 96),
])
def test_generated_host_buffer_api_and_adapter_todos(tmp_path, component, channels, frames):
    registry = Registry([ROOT / "components"])
    registry.scan()
    nodes = {
        "host_in": Node("host_in", f"orpheus.builtin.{component}", params={"channels": channels}),
        "host_out": Node("host_out", "orpheus.builtin.host_audio", params={"channels": channels, "mode": 1}),
    }
    drives = []
    if component == "host_audio":
        nodes["clock"] = Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 48000 // frames})
        drives.append(DriveConnection(PortRef("clock", "out"), PortRef("host_in", "in")))
    project = Project(tasks={"default": Task("default", sample_rate=48000, block_size=frames)},
        flow_mode="task_sequence", graph=Graph(nodes=nodes, connections=[
            Connection(PortRef("host_in", "out"), PortRef("host_out", "in"))]),
        drive_connections=drives)
    plan = GraphCompiler(registry).compile(project)
    generator = CodeGenerator(registry, ROOT)
    generated = tmp_path / "generated"
    generator.generate(plan, generated)
    symbol = generator._sanitized_node_id(plan.tasks[0]["id"])
    main = (generated / "src/main.c").read_text(encoding="utf-8")
    assert f"TODO: 宿主输入 host_in：{channels}ch × {frames}帧" in main
    assert f"TODO: 宿主输出 host_out：{channels}ch × {frames}帧" in main
    assert f"orpheus_graph_process_task_{symbol}" in main
    assert "不能填零冒充设备数据" in main
    harness = r'''
#include <assert.h>
#include <string.h>
#include "orpheus_graph.h"
int main(void) {
    float input[ORPHEUS_HOST_AUDIO_host_in_SAMPLES];
    float output[ORPHEUS_HOST_AUDIO_host_out_SAMPLES];
    assert(ORPHEUS_HOST_AUDIO_host_in_SAMPLES == EXPECTED_SAMPLES);
    for (unsigned i = 0; i < EXPECTED_SAMPLES; ++i) input[i] = (float)i * 0.001f;
    assert(orpheus_host_audio_submit_host_in(input, EXPECTED_SAMPLES) == ORPHEUS_ERR_PROCESSING);
    assert(orpheus_graph_init(ORPHEUS_GRAPH_SAMPLE_RATE, ORPHEUS_GRAPH_BLOCK_SIZE) == ORPHEUS_OK);
    assert(orpheus_graph_process(ORPHEUS_GRAPH_TICK) == ORPHEUS_ERR_PROCESSING);
    assert(strcmp(orpheus_graph_last_error()->node, "host_in") == 0);
    assert(orpheus_host_audio_read_host_out(output, EXPECTED_SAMPLES) == ORPHEUS_ERR_PROCESSING);
    assert(orpheus_host_audio_submit_host_in(NULL, EXPECTED_SAMPLES) == ORPHEUS_ERR_INVALID_ARG);
    assert(orpheus_host_audio_submit_host_in(input, EXPECTED_SAMPLES - 1) == ORPHEUS_ERR_INVALID_ARG);
    assert(orpheus_host_audio_submit_host_in(input, EXPECTED_SAMPLES + 1) == ORPHEUS_ERR_INVALID_ARG);
    assert(orpheus_host_audio_submit_host_in(input, EXPECTED_SAMPLES) == ORPHEUS_OK);
    assert(orpheus_host_audio_read_host_out(output, EXPECTED_SAMPLES) == ORPHEUS_ERR_PROCESSING);
    assert(orpheus_host_audio_submit_host_in(input, EXPECTED_SAMPLES) == ORPHEUS_ERR_PROCESSING);
    assert(orpheus_graph_last_error()->code == ORPHEUS_ERR_PROCESSING);
    assert(TASK_FUNCTION(ORPHEUS_GRAPH_TICK) == ORPHEUS_OK);
    assert(orpheus_host_audio_read_host_out(output, EXPECTED_SAMPLES - 1) == ORPHEUS_ERR_INVALID_ARG);
    assert(orpheus_host_audio_read_host_out(NULL, EXPECTED_SAMPLES) == ORPHEUS_ERR_INVALID_ARG);
    assert(orpheus_host_audio_read_host_out(output, EXPECTED_SAMPLES) == ORPHEUS_OK);
    assert(memcmp(input, output, sizeof(input)) == 0);
    assert(orpheus_host_audio_read_host_out(output, EXPECTED_SAMPLES) == ORPHEUS_ERR_PROCESSING);
    input[0] = -2.5f;
    assert(orpheus_host_audio_submit_host_in(input, EXPECTED_SAMPLES) == ORPHEUS_OK);
    assert(TASK_FUNCTION(ORPHEUS_GRAPH_TICK) == ORPHEUS_OK);
    assert(orpheus_host_audio_read_host_out(output, EXPECTED_SAMPLES) == ORPHEUS_OK);
    assert(memcmp(input, output, sizeof(input)) == 0);
    orpheus_graph_teardown();
    assert(orpheus_host_audio_submit_host_in(input, EXPECTED_SAMPLES) == ORPHEUS_ERR_PROCESSING);
    assert(orpheus_host_audio_read_host_out(output, EXPECTED_SAMPLES) == ORPHEUS_ERR_PROCESSING);
    return 0;
}
'''
    (generated / "src/host_api_test.c").write_text(harness, encoding="utf-8", newline="\n")
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8", newline="\n") as file:
        file.write("\nadd_executable(host_api_test src/host_api_test.c)\n"
            f"target_compile_definitions(host_api_test PRIVATE EXPECTED_SAMPLES={channels * frames} "
            f"TASK_FUNCTION=orpheus_generated_process_task_{symbol})\n"
            "target_link_libraries(host_api_test PRIVATE orpheus_graph)\n")
    _build(generated, tmp_path)
    result = subprocess.run([str(generated / "build/host_api_test.exe")],
        capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(generated / "build/orpheus_generated_app.exe"), "1"],
        capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 1
    assert "host_in" in result.stderr
    assert "图处理失败" in result.stderr


def test_generated_host_buffer_macros_keep_case_sensitive_instance_names(tmp_path):
    registry = Registry([ROOT / "components"])
    registry.scan()
    project = Project(flow_mode="task_sequence", graph=Graph(nodes={
        "clock": Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 1500}),
        "pcm": Node("pcm", "orpheus.builtin.host_audio", params={"channels": 2}),
        "PCM": Node("PCM", "orpheus.builtin.host_audio", params={"channels": 3}),
    }), drive_connections=[
        DriveConnection(PortRef("clock", "out"), PortRef(nid, "in")) for nid in ("pcm", "PCM")
    ], block_size=32, sample_rate=48000)
    plan = GraphCompiler(registry).compile(project)
    CodeGenerator(registry, ROOT).generate(plan, tmp_path)
    header = (tmp_path / "include/orpheus_graph.h").read_text(encoding="utf-8")
    assert "#define ORPHEUS_HOST_AUDIO_pcm_SAMPLES 64u" in header
    assert "#define ORPHEUS_HOST_AUDIO_PCM_SAMPLES 96u" in header


def execute_host_graph(tmp_path, registry, plan, inputs, outputs, *, blocks=24):
    generated = tmp_path / "generated"
    generator = CodeGenerator(registry, ROOT); generator.generate(plan, generated)
    symbol = generator._sanitized_node_id(plan.tasks[0]["id"])
    shutil.copy2(ROOT / "tests/host_audio_runner.cpp", generated / "src/host_audio_test.cpp")
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8", newline="\n") as file:
        cxx = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.+)$", (ROOT / "build/CMakeCache.txt").read_text(encoding="utf-8"), re.M)
        assert cxx
        file.write(f'\nset(CMAKE_CXX_COMPILER "{cxx[1]}" CACHE FILEPATH "" FORCE)\n'
            "enable_language(CXX)\nset(CMAKE_CXX_STANDARD 11)\n"
            "add_executable(host_audio_test src/host_audio_test.cpp)\n"
            "target_compile_definitions(host_audio_test PRIVATE ORPHEUS_TEST_GENERATED "
            f"ORPHEUS_TEST_TASK_FUNCTION=orpheus_generated_process_task_{symbol})\n"
            "target_link_libraries(host_audio_test PRIVATE orpheus_graph)\n")
        for component in sorted({c["component"] for c in plan.node_configs.values() if not c["component"].startswith("orpheus.builtin.")}):
            info = registry.get(component); assert info and info.package_type == "source"
            target = component.replace(".", "_") + "_dynamic"
            sources = " ".join('"' + (info.root_dir / name).as_posix() + '"' for name in info.manifest["sources"])
            file.write(f"\nadd_library({target} SHARED {sources})\n"
                f'target_include_directories({target} PRIVATE "{(info.root_dir / "include").as_posix()}")\n'
                f"target_compile_definitions({target} PRIVATE ORPHEUS_BUILDING_COMPONENT)\n"
                f'set_target_properties({target} PROPERTIES PREFIX "lib" OUTPUT_NAME "{component.replace(".", "_")}" '
                'WINDOWS_EXPORT_ALL_SYMBOLS ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
                'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")\n')
    components = sorted({c["component"] for c in plan.node_configs.values() if c["component"].startswith("orpheus.builtin.")})
    result = subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "host_audio_runner",
        *[c.replace(".", "_") for c in components]], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    _build(generated, tmp_path)
    libraries = generated / "build/components"
    libraries.mkdir(exist_ok=True)
    for component in components:
        for library in (ROOT / "build/components").glob("*" + component.replace(".", "_") + ".*"):
            if library.suffix in (".dll", ".so", ".dylib"): shutil.copy2(library, libraries / library.name)
    plan_path = tmp_path / "plan.json"; plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8", newline="\n")
    specs = [v for direction, nodes in (("I", inputs), ("O", outputs)) for node, channels in nodes.items()
             for v in (direction, node, str(channels))]
    step = plan.schedule["tick"]; results = []
    for generated_path in (False, True):
        executable = generated / "build/host_audio_test.exe" if generated_path else ROOT / "build/tests/host_audio_runner.exe"
        for mode in ("global", "task"):
            args = [mode, str(step), str(blocks), *specs]
            if not generated_path: args = [str(plan_path), str(libraries), *args]
            result = subprocess.run([str(executable), *args], cwd=tmp_path, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            results.append((tmp_path / "host_audio.bin").read_bytes())
    assert results == [results[0]] * 4
    assert len(results[0]) == blocks * step * sum(outputs.values()) * 4
    return results[0]


@pytest.mark.parametrize("channels", [3, 22, 30, 32])
def test_host_audio_bulk_commit_four_entries(tmp_path, channels):
    registry = Registry([ROOT / "components"]); registry.scan()
    nodes = {
        "clock": Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 1500}),
        "host_in": Node("host_in", "orpheus.builtin.host_audio", params={"channels": channels, "mode": 0}),
        "host_out": Node("host_out", "orpheus.builtin.host_audio", params={"channels": channels, "mode": 1}),
    }
    project = Project(tasks={"default": Task("default", sample_rate=48000, block_size=32)},
        flow_mode="task_sequence", graph=Graph(nodes=nodes, connections=[
            Connection(PortRef("host_in", "out"), PortRef("host_out", "in"))]),
        drive_connections=[DriveConnection(PortRef("clock", "out"), PortRef("host_in", "in")),
                           DriveConnection(PortRef("host_in", "out"), PortRef("host_out", "in"))])
    plan = GraphCompiler(registry).compile(project)
    data = execute_host_graph(tmp_path, registry, plan, {"host_in": channels}, {"host_out": channels})
    expected = [(((block*17+frame*3+channel*13) % 257)-128)*0.0009765625
                for block in range(24) for frame in range(32) for channel in range(channels)]
    assert data == struct.pack("<" + str(len(expected)) + "f", *expected)


def test_buffer_source_bulk_commit_four_entries(tmp_path):
    registry = Registry([ROOT / "components"])
    registry.scan()
    project = ProjectLoader(registry).load(ROOT / "examples/buffer_source_demo.yaml")
    plan = GraphCompiler(registry).compile(project)
    assert plan.tasks[0]["source"] == "source"
    assert plan.node_configs["source"]["input_ports"] == []
    assert plan.node_configs["source"]["output_ports"] == ["out"]
    data = execute_host_graph(tmp_path, registry, plan, {"source": 2}, {"capture": 2})
    expected = [(((block*17+frame*3+channel*13) % 257)-128)*0.0009765625
                for block in range(24) for frame in range(32) for channel in range(2)]
    assert data == struct.pack("<" + str(len(expected)) + "f", *expected)


def test_buffer_source_does_not_match_consumer_cadence():
    registry = Registry([ROOT / "components"])
    registry.scan()
    project = ProjectLoader(registry).load(ROOT / "examples/buffer_source_demo.yaml")
    project.drive_connections[0] = DriveConnection(PortRef("source", "out"), PortRef("gain", "in"), 4, 2)
    plan = GraphCompiler(registry).compile(project)
    assert plan.call_graph["source"] == [{"node": "gain", "every": 4, "phase": 2}]
    assert plan.node_configs["gain"]["frames"] == 32
    assert plan.node_configs["source"]["output_cadences"]["out"]["period"] == 1
    assert plan.node_configs["gain"]["period"] == 4
    project.flow_mode = "dataflow"
    with pytest.raises(CompileError, match="flow_mode: task_sequence"):
        GraphCompiler(registry).compile(project)
