"""非音频数据尺寸、同步依赖与最新值读取的编译及执行回归。"""

from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import asdict
from pathlib import Path

import pytest
import yaml

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import (
    Connection, Graph, Node, PortRef, Project, ProjectLoader, Task, project_to_dict,
)
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).parent / "fixtures" / "data_ports"


@pytest.fixture
def compiler():
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    registry.add_manifest(FIXTURE / "component.yaml")
    return GraphCompiler(registry)


def edge(source: str, target: str) -> Connection:
    return Connection(PortRef.parse(source), PortRef.parse(target))


def data_project(*, latest: bool = False) -> Project:
    project = Project(metadata={"name": "非音频数据链路"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=128)}
    nodes = [
        Node(id="clock", component="orpheus.builtin.signal_gen", params={"channels": 1}),
        Node(id="producer", component="orpheus.test.data_probe"),
        Node(id="consumer", component="orpheus.test.data_probe", params={"mode": 1}),
    ]
    connections = [
        edge("clock:out", "producer:audio"),
        edge("producer:vector_out", "consumer:vector"),
    ]
    if latest:
        nodes.extend([
            Node(id="batch", component="orpheus.builtin.downrate",
                 params={"factor": 3, "channels": 1}),
            Node(id="slow", component="orpheus.test.data_probe"),
        ])
        connections.extend([
            edge("clock:out", "batch:in"),
            edge("batch:out", "slow:audio"),
            edge("slow:scalar_out", "consumer:held"),
        ])
    else:
        connections.append(edge("producer:scalar_out", "consumer:scalar"))
    project.graph = Graph(nodes={node.id: node for node in nodes}, connections=connections)
    return project


def test_data_shapes_do_not_determine_processing_quantum(compiler):
    plan = compiler.compile(data_project())
    assert plan.schedule["tick"] == 128
    assert set(plan.schedule["periods"].values()) == {1}
    for node in ("producer", "consumer"):
        assert plan.node_configs[node]["frames"] == 128
        assert plan.node_configs[node]["block_size"] == 128
    vector = next(buf for buf in plan.buffers.values() if buf["to"] == "consumer:vector")
    scalar = next(buf for buf in plan.buffers.values() if buf["to"] == "consumer:scalar")
    assert (vector["type"], vector["channels"], vector["frame_count"]) == ("bulk", 2, 129)
    assert (scalar["type"], scalar["channels"], scalar["frame_count"]) == ("control", 1, 1)


def test_audio_and_data_inputs_can_have_different_sizes(compiler):
    project = data_project()
    project.graph.connections.append(edge("clock:out", "consumer:audio"))
    plan = compiler.compile(project)
    assert plan.node_configs["consumer"]["frames"] == 128


def test_latest_input_does_not_drive_consumer_clock(compiler):
    plan = compiler.compile(data_project(latest=True))
    assert plan.schedule["tick"] == 128
    assert plan.schedule["periods"]["consumer"] == 1
    assert plan.schedule["periods"]["slow"] == 3
    assert plan.node_configs["consumer"]["frames"] == 128
    assert plan.execution_order.index("slow") < plan.execution_order.index("consumer")
    held = next(buf for buf in plan.buffers.values() if buf["to"] == "consumer:held")
    assert held["timing"] == "latest"
    assert held["frame_count"] == 1
    assert not held.get("rate_bridge")
    assert not held.get("task_bridge")


def test_sync_data_inputs_with_different_delivery_periods_are_rejected(compiler):
    project = data_project(latest=True)
    project.graph.connections[-1] = edge("slow:scalar_out", "consumer:scalar")
    with pytest.raises(CompileError, match="rate mismatch|交付周期"):
        compiler.compile(project)


@pytest.mark.parametrize("field,value", [("block_size", 128), ("channels", 3)])
def test_data_shape_mismatch_is_rejected(compiler, field, value):
    info = compiler.registry.get("orpheus.test.data_probe")
    next(port for port in info.manifest["ports"] if port["id"] == "vector")[field] = value
    with pytest.raises(CompileError, match="数据形状|channels mismatch"):
        compiler.compile(data_project())


@pytest.mark.parametrize("port_id", ["audio", "scalar_out"])
def test_latest_is_only_valid_on_non_audio_inputs(compiler, port_id):
    info = compiler.registry.get("orpheus.test.data_probe")
    next(port for port in info.manifest["ports"] if port["id"] == port_id)["timing"] = "latest"
    with pytest.raises(CompileError, match="latest.*非音频输入"):
        compiler.compile(data_project())


def test_latest_cannot_cross_tasks_without_a_bridge(compiler):
    project = data_project(latest=True)
    project.tasks["other"] = Task(id="other", sample_rate=48000, block_size=128)
    project.graph.nodes["consumer"].task = "other"
    with pytest.raises(CompileError, match="跨 Task"):
        compiler.compile(project)


def test_data_only_chain_inherits_fractional_audio_timeline(compiler):
    project = data_project()
    project.graph.nodes["batch"] = Node(
        id="batch", component="orpheus.builtin.downrate", params={"factor": 13, "channels": 1},
    )
    project.graph.nodes["src"] = Node(
        id="src", component="orpheus.builtin.rational_resample",
        params={"up": 3, "down": 13, "channels": 1},
    )
    project.graph.connections[:1] = [
        edge("clock:out", "batch:in"), edge("batch:out", "src:in"),
        edge("src:out", "producer:audio"),
    ]
    plan = compiler.compile(project)
    assert plan.node_configs["consumer"]["frames"] == 384
    assert plan.node_configs["consumer"]["sample_rate_ratio"] == [144000, 13]
    assert plan.schedule["periods"]["consumer"] == 13


def test_data_shape_defaults_to_scalar_not_audio_block(compiler):
    info = compiler.registry.get("orpheus.test.data_probe")
    for port in info.manifest["ports"]:
        if port["type"] != "audio":
            port.pop("block_size")
            port.pop("channels")
    plan = compiler.compile(data_project())
    assert all(buf["frame_count"] == 1 and buf["channels"] == 1
               for buf in plan.buffers.values() if buf["type"] != "audio")
    assert plan.node_configs["consumer"]["frames"] == 128


def test_data_sample_rate_metadata_does_not_change_timeline(compiler):
    info = compiler.registry.get("orpheus.test.data_probe")
    for port in info.manifest["ports"]:
        if port["type"] != "audio":
            port["sample_rate"] = 17 if port["direction"] == "input" else 23
    plan = compiler.compile(data_project())
    assert plan.node_configs["consumer"]["sample_rate"] == 48000
    assert plan.node_configs["consumer"]["frames"] == 128
    assert plan.schedule["tick"] == 128


def test_non_audio_formats_are_not_silently_treated_as_float(compiler):
    info = compiler.registry.get("orpheus.test.data_probe")
    for port in info.manifest["ports"]:
        if port["type"] != "audio":
            port["sample_format"] = "i32"
    with pytest.raises(CompileError, match="仅支持 f32"):
        compiler.compile(data_project())


def test_data_only_sources_use_task_clock(compiler):
    project = data_project()
    del project.graph.nodes["clock"]
    project.graph.connections = project.graph.connections[1:]
    plan = compiler.compile(project)
    assert plan.schedule["tick"] == 128
    assert plan.node_configs["consumer"]["frames"] == 128


def test_latest_edges_do_not_hide_data_cycles(compiler):
    project = data_project(latest=True)
    project.graph.connections.append(edge("consumer:scalar_out", "producer:held"))
    with pytest.raises(CompileError, match="cycle"):
        compiler.compile(project)


def test_audio_after_data_chain_preserves_fractional_rate(compiler):
    project = data_project()
    project.graph.nodes["src"] = Node(
        id="src", component="orpheus.builtin.rational_resample",
        params={"up": 1, "down": 3, "channels": 1},
    )
    project.tasks["default"].block_size = 126
    project.tasks["default"].sample_rate = 32000
    project.graph.connections[0:1] = [
        edge("clock:out", "src:in"), edge("src:out", "producer:audio"),
    ]
    project.graph.nodes["out"] = Node(
        id="out", component="orpheus.builtin.gain", params={"channels": 1},
    )
    project.graph.connections.append(edge("consumer:out", "out:in"))
    plan = compiler.compile(project)
    for name in ("producer", "consumer", "out"):
        assert plan.node_configs[name]["frames"] == 42
        assert plan.node_configs[name]["sample_rate_ratio"] == [32000, 3]
        assert plan.schedule["periods"][name] == 1


def test_unknown_data_source_port_reports_compile_error(compiler):
    project = data_project()
    project.graph.connections[1] = edge("producer:missing", "consumer:vector")
    with pytest.raises(CompileError, match="unknown source port"):
        compiler.compile(project)


def test_composite_data_ports_survive_load_save_and_flatten(compiler, tmp_path):
    fixture_manifest = compiler.registry.get("orpheus.test.data_probe").manifest
    wrapper = {
        "id": "orpheus.test.data_wrapper", "version": "1.0.0", "abi_version": 5,
        "package_type": "composite",
        "ports": [{**port, "maps_to": f"inner:{port['id']}"}
                  for port in fixture_manifest["ports"]],
        "parameters": [],
        "graph": {
            "nodes": [{"id": "inner", "component": "orpheus.test.data_probe",
                       "params": {"mode": 1}}],
            "connections": [],
        },
    }
    manifest_path = tmp_path / "wrapper.component.yaml"
    manifest_path.write_text(yaml.safe_dump(wrapper), encoding="utf-8")
    project = data_project(latest=True)
    project.graph.nodes["consumer"].component = wrapper["id"]
    project.graph.nodes["consumer"].params = {}
    project.imports = ["wrapper.component.yaml"]
    path = tmp_path / "project.yaml"
    path.write_text(yaml.safe_dump(project_to_dict(project)), encoding="utf-8")
    loaded = ProjectLoader(compiler.registry).load(path)
    path.write_text(yaml.safe_dump(project_to_dict(loaded)), encoding="utf-8")
    loaded = ProjectLoader(compiler.registry).load(path)
    plan = GraphCompiler(loaded.registry).compile(flatten_project(loaded))
    assert plan.schedule["periods"]["consumer__inner"] == 1
    assert plan.node_configs["consumer__inner"]["frames"] == 128
    assert plan.node_configs["consumer__inner"]["input_port_timings"]["held"] == "latest"
    vector = next(buf for buf in plan.buffers.values()
                  if buf["to"] == "consumer__inner:vector")
    assert (vector["type"], vector["frame_count"], vector["channels"]) == ("bulk", 129, 2)


@pytest.mark.parametrize("latest", [False, True])
def test_data_ports_dynamic_generated_values_and_timeline(compiler, tmp_path, latest):
    from test_generated_double_bank import _build, _run

    project = data_project(latest=latest)
    project.graph.nodes["fanout"] = Node(
        id="fanout", component="orpheus.test.data_probe", params={"mode": 1},
    )
    project.graph.connections.extend([
        edge("producer:vector_out", "fanout:vector"),
        edge("producer:scalar_out", "fanout:scalar"),
    ])
    project.graph.nodes = dict(reversed(list(project.graph.nodes.items())))
    plan = compiler.compile(project)
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = tmp_path / "generated"
    CodeGenerator(compiler.registry, ROOT).generate(plan, generated)
    # DLL 与静态图使用同一份验收组件源码，不把测试组件加入标准组件目录。
    cmake = generated / "CMakeLists.txt"
    cxx_compiler = next(
        line.split("=", 1)[1]
        for line in (ROOT / "build" / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
        if line.startswith("CMAKE_CXX_COMPILER:") and "=" in line
    )
    archives = [
        ROOT / "build" / "orpheus_runtime" / "liborpheus_runtime_lib.a",
        ROOT / "build" / "orpheus_abi" / "liborpheus_bridge_endpoint.a",
        ROOT / "build" / "orpheus_abi" / "liborpheus_olink.a",
    ]
    cmake.write_text(
        cmake.read_text(encoding="utf-8") + "\n"
        + f'add_library(data_probe_dynamic SHARED "{(FIXTURE / "src" / "data_probe.c").as_posix()}")\n'
        + f'target_include_directories(data_probe_dynamic PRIVATE "{(FIXTURE / "include").as_posix()}")\n'
        + 'set_target_properties(data_probe_dynamic PROPERTIES PREFIX "lib" '
          'OUTPUT_NAME "orpheus_test_data_probe" WINDOWS_EXPORT_ALL_SYMBOLS ON '
          'RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
          'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")\n'
        + f'set(CMAKE_CXX_COMPILER "{Path(cxx_compiler).as_posix()}")\n'
        + 'enable_language(CXX)\n'
        + 'if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")\n'
        + '  target_compile_options(data_probe_dynamic PRIVATE -Werror=incompatible-pointer-types)\n'
        + '  target_compile_options(orpheus_test_data_probe PRIVATE -Werror=incompatible-pointer-types)\n'
        + 'endif()\n'
        + f'add_executable(data_runtime_task "{(FIXTURE / "runtime_task.cpp").as_posix()}")\n'
        + f'target_include_directories(data_runtime_task PRIVATE "{(ROOT / "orpheus_runtime" / "include").as_posix()}")\n'
        + 'target_link_libraries(data_runtime_task PRIVATE '
        + " ".join(f'"{archive.as_posix()}"' for archive in archives) + ')\n'
        + f'add_executable(data_generated_task "{(FIXTURE / "generated_task.c").as_posix()}")\n'
        + 'target_link_libraries(data_generated_task PRIVATE orpheus_graph)\n',
        encoding="utf-8",
    )
    generated_exe = _build(generated, tmp_path)
    components = generated / "build" / "components"
    for name in ("signal_gen", "downrate"):
        for library in (ROOT / "build" / "components").glob(f"*orpheus_builtin_{name}.*"):
            if library.suffix in (".dll", ".so", ".dylib"):
                shutil.copy2(library, components / library.name)
    amplitude_id = next(item["id"] for item in plan.id_map
                        if item["node"] == "clock" and item["key"] == "amplitude")
    for node in ("consumer", "fanout"):
        dynamic = subprocess.run(
            [str(ROOT / "build" / "orpheus_runtime.exe"), str(plan_path), str(components),
             "--rw", str(amplitude_id), "0.1", "--run", "7", "--getbulk", node, "metrics"],
            cwd=tmp_path, capture_output=True, text=True, encoding="utf-8",
        )
        assert dynamic.returncode == 0, dynamic.stdout + dynamic.stderr
        static = _run(generated_exe, tmp_path, "0", "--run", "7",
                      "--read-bulk", node, "metrics")
        assert static.returncode == 0, static.stdout + static.stderr
        dynamic_task = _run(generated / "build" / "data_runtime_task.exe", tmp_path,
                            str(plan_path), str(components), node)
        static_task = _run(generated / "build" / "data_generated_task.exe", tmp_path, node)
        rows = []
        for result in (dynamic, static, dynamic_task, static_task):
            assert result.returncode == 0, result.stdout + result.stderr
            line = next((line for line in result.stdout.splitlines()
                         if line.startswith("BULKVALUE ")), None)
            assert line is not None, result.stdout + result.stderr
            # 动态协议输出 ID，生成 CLI 输出 node/key。
            start = 2 if result is dynamic else 3
            rows.append([float(value) for value in line.split()[start:]])
        assert all(row == rows[0] for row in rows)
        metrics = rows[0]
        assert len(metrics) == 32
        assert metrics[:7] == [7, 128, 768, 128, 129, 2, 1]
        expected = [
            33153 + 258 * call
            + (10 * (call // 3) if latest and node == "consumer" else 10 * call)
            for call in range(1, 8)
        ]
        assert metrics[8:15] == expected
