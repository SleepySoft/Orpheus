"""静态调度表（clock chain / static schedule）测试。

背景：docs/design_clock_scheduling.md —— 多速率图里不同分支块长不同（如 24/32/96），
宿主却用单一全局 plan.block_size 推进，薄 sink（wav_out）每个宿主 tick 都被调用并按
输入缓冲容量整块落盘，导致同一块被重复写（2s 应得 96000 帧，实际写出 4 倍）。

修复：compiler 推导静态调度表（主步长 tick = 各节点触发间隔的 GCD + 每节点周期），
host 按 tick 推进；跨速率合流经 rate-bridge FIFO（深度=合流节点 LCM 量子）。
"""

from __future__ import annotations

import math
import uuid
import wave
from pathlib import Path

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def compiler():
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    return GraphCompiler(registry)


def conn(a: str, b: str) -> Connection:
    return Connection(from_ref=PortRef.parse(a), to_ref=PortRef.parse(b))


def make_merge_project() -> Project:
    """signal_gen(块24) + signal_gen(块32) -> rate_sync(LCM 96) -> wav_out。"""
    project = Project(metadata={"name": "t"})
    project.tasks = {
        "tidA": Task(id="tidA", sample_rate=48000, block_size=24),
        "tidB": Task(id="tidB", sample_rate=48000, block_size=32),
    }
    nodes = [
        Node(id="a", component="orpheus.builtin.signal_gen", task="tidA",
             params={"frequency": 100.0, "amplitude": 0.1, "channels": 1, "duration_s": 2.0}),
        Node(id="b", component="orpheus.builtin.signal_gen", task="tidB",
             params={"frequency": 100.0, "amplitude": 0.1, "channels": 1, "duration_s": 2.0}),
        Node(id="sync", component="orpheus.builtin.rate_sync", task="tidA",
             params={"channels": 1, "mode": 0, "buffer_length": 0}),
        Node(id="o", component="orpheus.builtin.wav_out", task="tidA",
             params={"channels": 1, "sample_rate": 48000, "file_path": "out.wav"}),
    ]
    project.graph = Graph(
        nodes={n.id: n for n in nodes},
        connections=[conn("a:out", "sync:in0"), conn("b:out", "sync:in1"),
                     conn("sync:out", "o:in")],
    )
    return project


def test_merge_schedule_table(compiler):
    """合并图应推导出主步长=GCD(24,32,96)=8，周期=3/4/12/12 的静态调度表。"""
    plan = compiler.compile(make_merge_project())
    schedule = getattr(plan, "schedule", None)
    assert schedule is not None, "plan 缺少静态调度表（schedule）"
    assert schedule["tick"] == 8
    periods = schedule["periods"]
    assert periods["a"] == 3
    assert periods["b"] == 4
    assert periods["sync"] == 12
    assert periods["o"] == 12
    # sink 不再每 tick 触发：每 12 个主 tick 消费一个 96 帧块
    assert plan.node_configs["o"]["frames"] == 96

    tasks = {task["id"]: task for task in plan.tasks}
    assert list(tasks) == ["tidA", "tidB"]
    assert tasks["tidA"]["execution_order"] == ["a", "sync", "o"]
    assert tasks["tidA"]["schedule"] == {
        "tick": 24,
        "periods": {"a": 1, "sync": 4, "o": 4},
    }
    assert tasks["tidB"]["execution_order"] == ["b"]
    assert tasks["tidB"]["schedule"] == {"tick": 32, "periods": {"b": 1}}


def test_single_rate_schedule_matches_legacy(compiler):
    """单速率图：主步长必须等于 plan.block_size，周期等于旧 divisor（行为不变）。"""
    project = Project(metadata={"name": "t"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=128)}
    nodes = [
        Node(id="gen", component="orpheus.builtin.signal_gen",
             params={"frequency": 440.0, "amplitude": 0.1, "channels": 1}),
        Node(id="dr", component="orpheus.builtin.downrate",
             params={"factor": 4, "channels": 1}),
        Node(id="g", component="orpheus.builtin.gain",
             params={"gain_db": "0.0", "channels": 1}),
        Node(id="o", component="orpheus.builtin.wav_out",
             params={"channels": 1, "sample_rate": 48000, "file_path": "out.wav"}),
    ]
    project.graph = Graph(
        nodes={n.id: n for n in nodes},
        connections=[conn("gen:out", "dr:in"), conn("dr:out", "g:in"),
                     conn("g:out", "o:in")],
    )
    plan = compiler.compile(project)
    schedule = getattr(plan, "schedule", None)
    assert schedule is not None, "plan 缺少静态调度表（schedule）"
    assert schedule["tick"] == plan.block_size == 128
    for nid, cfg in plan.node_configs.items():
        assert schedule["periods"][nid] == cfg["divisor"], nid


def test_generated_task_entries(compiler, tmp_path):
    plan = compiler.compile(make_merge_project())
    out = tmp_path / "generated"
    CodeGenerator(compiler.registry, ROOT).generate(plan, out)

    header = (out / "include" / "orpheus_generated.h").read_text(encoding="utf-8")
    source = (out / "src" / "main.c").read_text(encoding="utf-8")
    for task_id in ("tidA", "tidB"):
        signature = f"int orpheus_generated_process_task_{task_id}(uint32_t frame_count)"
        assert signature + ";" in header
        assert signature + " {" in source
    assert "(g_task_counter_tidA + 1) % 4 == 0" in source
    assert "g_task_counter_tidB++;" in source
    assert "void orpheus_generated_teardown(void)" in header


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists(),
    reason="runtime not built",
)
def test_merge_sink_no_duplicate_writes():
    """复现并回归：跨块长合流图的 wav_out 不得重复写（2s@48k 应得 96000 帧）。

    修复前：宿主按 plan.block_size=24 推进，wav_out（frames=96, divisor=1）每 tick
    触发一次、每次整块写 96 帧 → 实际写出 4×96000 帧。
    """
    import shutil

    from fastapi.testclient import TestClient

    from orpheus_core.server.app import create_app

    name = f"test_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            pdir = ROOT / "workspace" / name
            doc = client.get(f"/api/projects/{name}").json()
            doc["tasks"] = [
                {"id": "tidA", "name": "A", "sample_rate": 48000, "block_size": 24, "priority": 0},
                {"id": "tidB", "name": "B", "sample_rate": 48000, "block_size": 32, "priority": 0},
            ]
            doc["graph"] = {
                "nodes": [
                    {"id": "a", "component": "orpheus.builtin.signal_gen", "task": "tidA",
                     "params": {"frequency": 100.0, "amplitude": 0.1, "channels": 1,
                                "duration_s": 2.0},
                     "position": {"x": 0, "y": 0}},
                    {"id": "b", "component": "orpheus.builtin.signal_gen", "task": "tidB",
                     "params": {"frequency": 200.0, "amplitude": 0.1, "channels": 1,
                                "duration_s": 2.0},
                     "position": {"x": 0, "y": 100}},
                    {"id": "sync", "component": "orpheus.builtin.rate_sync", "task": "tidA",
                     "params": {"channels": 1, "mode": 0, "buffer_length": 0},
                     "position": {"x": 200, "y": 50}},
                    {"id": "o", "component": "orpheus.builtin.wav_out", "task": "tidA",
                     "params": {"file_path": "outputs/out.wav", "channels": 1,
                                "sample_rate": 48000},
                     "position": {"x": 400, "y": 50}},
                ],
                "connections": [
                    {"from": "a:out", "to": "sync:in0"},
                    {"from": "b:out", "to": "sync:in1"},
                    {"from": "sync:out", "to": "o:in"},
                ],
            }
            assert client.put(f"/api/projects/{name}", json=doc).status_code == 200

            resp = client.post(f"/api/projects/{name}/run")
            assert resp.json()["status"] == "ok", resp.json()
            out_wav = pdir / "outputs" / "out.wav"
            with wave.open(str(out_wav), "rb") as w:
                frames = w.getnframes()
            expected = 2 * 48000
            assert abs(frames - expected) <= 96, (
                f"wav_out 写入 {frames} 帧，期望 {expected}（重复写 {frames / expected:.1f}x）"
            )
            dynamic_bytes = out_wav.read_bytes()

            # 生成路径必须与动态路径逐字节一致
            resp = client.post(f"/api/projects/{name}/run_generated")
            result = resp.json()
            assert result["status"] == "ok", result["stderr"]
            assert out_wav.read_bytes() == dynamic_bytes
        finally:
            client.delete(f"/api/projects/{name}")
            shutil.rmtree(pdir, ignore_errors=True)
