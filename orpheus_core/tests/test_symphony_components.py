"""Symphony BAF 结构参考的组件覆盖。"""

from __future__ import annotations

from pathlib import Path

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "symphony_baf_structural_reference.yaml"


def test_symphony_components_compile() -> None:
    """完整工程所需的专用组件均进入编译执行计划。"""
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = flatten_project(ProjectLoader().load(EXAMPLE))
    plan = GraphCompiler(registry).compile(project)
    comps = {cfg["component"] for cfg in plan.node_configs.values()}
    for cid in (
        "orpheus.builtin.gain_ramper",
        "orpheus.builtin.iir_bank",
        "orpheus.builtin.sleeping_beauty",
        "orpheus.builtin.input_mixer_3d",
        "orpheus.builtin.baf_soft_clipper",
        "orpheus.builtin.fir",
    ):
        assert cid in comps, f"missing {cid}"
