"""多 Task 异步桥：编译约束、计划元数据与双路径行为。"""

from __future__ import annotations

import json
import subprocess
import wave
from dataclasses import asdict
from pathlib import Path

import pytest

from orpheus_core.builder import run_cmake_with_msvc_env
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def compiler() -> GraphCompiler:
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    return GraphCompiler(registry)


def connection(source: str, target: str) -> Connection:
    return Connection(PortRef.parse(source), PortRef.parse(target))


def project_with_bridge(use_bridge: bool = True) -> Project:
    project = Project(metadata={"name": "async_bridge"})
    project.tasks = {
        "producer": Task(id="producer", sample_rate=48000, block_size=24),
        "consumer": Task(id="consumer", sample_rate=48000, block_size=32),
    }
    source = Node(
        id="src", component="orpheus.builtin.signal_gen", task="producer",
        params={"frequency": 440.0, "amplitude": 0.25, "channels": 1},
    )
    sink = Node(
        id="sink", component="orpheus.builtin.null_sink", task="consumer",
        params={"channels": 1},
    )
    nodes = [source, sink]
    connections = []
    if use_bridge:
        bridge = Node(
            id="bridge", component="orpheus.builtin.async_bridge", task="consumer",
            params={"channels": 1, "capacity_frames": 0},
        )
        nodes.append(bridge)
        connections.extend([
            connection("src:out", "bridge:in"),
            connection("bridge:out", "sink:in"),
        ])
    else:
        connections.append(connection("src:out", "sink:in"))
    project.graph = Graph(nodes={node.id: node for node in nodes}, connections=connections)
    return project


def test_direct_cross_task_connection_is_rejected(compiler: GraphCompiler) -> None:
    with pytest.raises(CompileError, match="跨 Task 连接必须经过异步任务桥"):
        compiler.compile(project_with_bridge(use_bridge=False))


def test_async_bridge_plan_metadata(compiler: GraphCompiler) -> None:
    plan = compiler.compile(project_with_bridge())
    edge = next(buffer for buffer in plan.buffers.values() if buffer.get("task_bridge"))
    assert edge["from"] == "src:out"
    assert edge["to"] == "bridge:in"
    assert edge["producer_frames"] == 24
    assert edge["frame_count"] == 32
    assert edge["capacity_frames"] == 192
    assert plan.node_configs["bridge"]["frames"] == 32


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components" / "liborpheus_builtin_async_bridge.dll").exists(),
    reason="runtime and async_bridge component not built",
)
def test_runtime_task_entries_transfer_through_spsc(compiler: GraphCompiler, tmp_path: Path) -> None:
    project = project_with_bridge()
    project.graph.nodes["sink"] = Node(
        id="sink", component="orpheus.builtin.wav_out", task="consumer",
        params={"channels": 1, "sample_rate": 48000, "file_path": "out.wav"},
    )
    plan = compiler.compile(project)
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan), ensure_ascii=False), encoding="utf-8")
    task_args = ["--task", "producer", "4", "--task", "consumer", "3"] * 50

    result = subprocess.run(
        [
            str(ROOT / "build" / "orpheus_runtime.exe"),
            str(plan_path),
            str(ROOT / "build" / "components"),
            *task_args,
        ],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    assert result.returncode == 0, result.stderr
    with wave.open(str(tmp_path / "out.wav"), "rb") as output:
        assert output.getnframes() == 4800
        assert any(output.readframes(output.getnframes()))
    dynamic_bytes = (tmp_path / "out.wav").read_bytes()

    generated = tmp_path / "generated"
    CodeGenerator(compiler.registry, ROOT).generate(plan, generated)
    generated_source = (generated / "src" / "main.c").read_text(encoding="utf-8")
    assert "static OrpheusAtomicU64 g_task_underruns_0" in generated_source
    assert "static OrpheusAtomicU64 g_task_overruns_0" in generated_source
    build_dir = generated / "build"
    configure = run_cmake_with_msvc_env(
        ["cmake", "-S", str(generated), "-B", str(build_dir), "-G", "Ninja"],
        generated,
        build_dir,
    )
    assert configure.returncode == 0, configure.stdout + configure.stderr
    build = run_cmake_with_msvc_env(
        ["cmake", "--build", str(build_dir)], build_dir, build_dir
    )
    assert build.returncode == 0, build.stdout + build.stderr
    executable = build_dir / "orpheus_generated_app.exe"
    generated_run = subprocess.run(
        [
            str(executable),
            *task_args,
        ],
        cwd=generated,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    assert generated_run.returncode == 0, generated_run.stderr
    generated_bytes = (generated / "out.wav").read_bytes()
    assert generated_bytes == dynamic_bytes
    assert plan.node_configs["bridge"]["task"] == "consumer"
