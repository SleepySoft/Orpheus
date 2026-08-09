"""声明式平台节点（platform_hook）测试。

覆盖：不进入执行计划（plan.declarations）；不约束平台可达性；
生成 init/read/write 钩子（USER CODE 填充）与头文件。
"""

from __future__ import annotations

from pathlib import Path

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import (
    Connection,
    Graph,
    Node,
    PortRef,
    Project,
)
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture()
def registry():
    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    return reg


def _project() -> Project:
    project = Project()
    project.graph = Graph(
        nodes={
            n.id: n
            for n in [
                Node(id="wav_in", component="orpheus.builtin.wav_in", params={"file_path": "input.wav", "channels": 1}),
                Node(id="g", component="orpheus.builtin.gain", params={"channels": 1}),
                Node(id="out", component="orpheus.builtin.wav_out", params={"file_path": "outputs/out.wav", "channels": 1, "sample_rate": 48000}),
                Node(
                    id="hook",
                    component="orpheus.builtin.platform_hook",
                    params={"hook_name": "eq_gain", "interface": "amixer", "note": "ALSA 主音量"},
                ),
            ]
        },
        connections=[
            Connection(PortRef.parse("wav_in:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("out:in")),
        ],
    )
    return project


def test_platform_hook_excluded_from_plan(registry) -> None:
    """platform_hook 不进入执行计划，仅进 plan.declarations。"""
    plan = GraphCompiler(registry).compile(_project())
    assert "hook" not in plan.nodes
    assert "hook" not in plan.execution_order
    assert len(plan.declarations) == 1
    decl = plan.declarations[0]
    assert decl["id"] == "hook"
    assert decl["component"] == "orpheus.builtin.platform_hook"
    assert decl["params"]["interface"] == "amixer"
    assert plan.execution_order == ["wav_in", "g", "out"]


def test_platform_hook_does_not_constrain_platform(registry) -> None:
    """声明节点不参与平台可达性：device_in(win) + hook 在 win 目标下可编译。"""
    project = _project()
    project.graph.nodes["wav_in"] = Node(id="wav_in", component="orpheus.builtin.device_in", params={"channels": 1})
    plan = GraphCompiler(registry).compile(project, "win")
    assert "hook" not in plan.execution_order
    assert plan.execution_order == ["wav_in", "g", "out"]


def test_platform_hook_generates_hooks(tmp_path, registry) -> None:
    """生成工程包含 orpheus_platform_hooks.h / platform_hooks.c（USER CODE 填充）。"""
    plan = GraphCompiler(registry).compile(_project())
    gen = tmp_path / "gen"
    CodeGenerator(registry, ROOT).generate(plan, gen)
    hdr = (gen / "include" / "orpheus_platform_hooks.h").read_text(encoding="utf-8")
    src = (gen / "src" / "platform_hooks.c").read_text(encoding="utf-8")
    cmake = (gen / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "orpheus_platform_eq_gain_init" in hdr
    assert "orpheus_platform_eq_gain_read" in hdr
    assert "orpheus_platform_eq_gain_write" in hdr
    assert "interface=amixer" in src
    assert "USER CODE BEGIN init" in src
    assert "USER CODE END write" in src
    assert "src/platform_hooks.c" in cmake
