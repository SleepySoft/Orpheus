"""真实宿主缓冲接口；缺块报错，不用置零代替输入。"""
from dataclasses import asdict
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project, Task
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
