"""目标平台与 alter 组解析测试。

覆盖：无指定平台组件全平台可用；alter 组按 win/dsp 目标选择激活成员并重映射连线；
整链平台不可达报错（列出冲突/断链节点）；alter 接口不一致报错；loader 往返保留。
"""

from __future__ import annotations

from pathlib import Path

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import (
    Connection,
    Graph,
    Node,
    PortRef,
    Project,
    ProjectLoader,
)
from orpheus_core.registry import Registry
from orpheus_core.resolve import ResolutionError, resolve_project

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture()
def registry():
    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    return reg


def _project(nodes: list[Node], connections: list[Connection], target: str = "auto") -> Project:
    project = Project(target=target)
    project.graph = Graph(nodes={n.id: n for n in nodes}, connections=connections)
    return project


def _node(node_id: str, component: str, alters: list[str] | None = None) -> Node:
    return Node(id=node_id, component=component, alters=list(alters or []))


def test_unspecified_components_support_all(registry) -> None:
    """未声明 platforms 的组件（如 gain）在 win 和 dsp 下都可用。"""
    from orpheus_core.resolve import component_supports, load_platforms

    platforms = load_platforms()
    gain = registry.get("orpheus.builtin.gain")
    assert gain is not None and gain.platforms == []
    assert component_supports(gain, "win", platforms)
    assert component_supports(gain, "dsp", platforms)
    # 平台受限组件只在声明平台可用
    dev_in = registry.get("orpheus.builtin.device_in")
    embed_in = registry.get("orpheus.builtin.embed_in")
    assert component_supports(dev_in, "win", platforms)
    assert not component_supports(dev_in, "dsp", platforms)
    assert component_supports(embed_in, "dsp", platforms)
    assert not component_supports(embed_in, "win", platforms)


def test_alter_group_resolves_per_target(registry) -> None:
    """device_in(win) 与 embed_in(dsp) 互为 alter：按目标选激活成员、重映射连线。"""
    project = _project(
        nodes=[
            _node("in_pc", "orpheus.builtin.device_in", alters=["in_dsp"]),
            _node("in_dsp", "orpheus.builtin.embed_in"),
            _node("g", "orpheus.builtin.gain"),
            _node("out", "orpheus.builtin.wav_out"),
        ],
        connections=[
            Connection(PortRef.parse("in_pc:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("out:in")),
        ],
    )

    # win 目标：激活锚定成员 in_pc，in_dsp 被移除
    resolved, res = resolve_project(project, registry, "win")
    assert res.platform == "win"
    assert set(resolved.graph.nodes) == {"in_pc", "g", "out"}
    assert str(resolved.graph.connections[0].from_ref) == "in_pc:out"

    # dsp 目标：激活 in_dsp，连线从锚定 in_pc 重映射到 in_dsp
    resolved, res = resolve_project(project, registry, "dsp")
    assert res.platform == "dsp"
    assert set(resolved.graph.nodes) == {"in_dsp", "g", "out"}
    assert str(resolved.graph.connections[0].from_ref) == "in_dsp:out"
    assert res.active == {"in_pc": "in_dsp"}
    assert res.warnings


def test_chain_break_reports_conflicts(registry) -> None:
    """device_in(仅 win) -> gain -> embed_out(仅 dsp)：无统一平台，报错列出节点。"""
    project = _project(
        nodes=[
            _node("src", "orpheus.builtin.device_in"),
            _node("g", "orpheus.builtin.gain"),
            _node("sink", "orpheus.builtin.embed_out"),
        ],
        connections=[
            Connection(PortRef.parse("src:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("sink:in")),
        ],
    )
    with pytest.raises(ResolutionError) as exc:
        resolve_project(project, registry, "auto")
    msg = str(exc.value)
    assert "无统一平台" in msg
    assert "src" in msg and "sink" in msg


def test_target_not_supported_reports_broken_nodes(registry) -> None:
    """整链仅 win 可用但期望 dsp：报错并列出不支持 dsp 的节点。"""
    project = _project(
        nodes=[
            _node("src", "orpheus.builtin.device_in"),
            _node("g", "orpheus.builtin.gain"),
            _node("out", "orpheus.builtin.wav_out"),
        ],
        connections=[
            Connection(PortRef.parse("src:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("out:in")),
        ],
    )
    with pytest.raises(ResolutionError) as exc:
        resolve_project(project, registry, "dsp")
    msg = str(exc.value)
    assert "dsp 不可达" in msg
    assert "src" in msg


def test_alter_compliance_rejects_incompatible(registry) -> None:
    """gain 与 mixer 端口集合不同：alter 组非法，报接口不一致。"""
    project = _project(
        nodes=[
            _node("a", "orpheus.builtin.gain", alters=["b"]),
            _node("b", "orpheus.builtin.mixer"),
            _node("out", "orpheus.builtin.wav_out"),
        ],
        connections=[],
    )
    with pytest.raises(ResolutionError) as exc:
        resolve_project(project, registry, "win")
    assert "接口不一致" in str(exc.value)


def test_alter_multiple_wired_members_rejected(registry) -> None:
    """alter 组内多个成员参与连线：报错（同一槽位只能连一个）。"""
    project = _project(
        nodes=[
            _node("in_pc", "orpheus.builtin.device_in", alters=["in_dsp"]),
            _node("in_dsp", "orpheus.builtin.embed_in"),
            _node("g", "orpheus.builtin.gain"),
            _node("out", "orpheus.builtin.wav_out"),
        ],
        connections=[
            Connection(PortRef.parse("in_pc:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("in_dsp:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("out:in")),
        ],
    )
    with pytest.raises(ResolutionError) as exc:
        resolve_project(project, registry, "win")
    assert "多个成员参与连线" in str(exc.value)


def test_compile_resolves_alter_group(registry) -> None:
    """编译路径：dsp 目标下 alter 组解析后参与编译的是 embed_in。"""
    io_params = {"channels": 2, "sample_rate": 48000}
    project = _project(
        nodes=[
            Node(id="in_pc", component="orpheus.builtin.device_in", alters=["in_dsp"], params=io_params),
            Node(id="in_dsp", component="orpheus.builtin.embed_in", params=io_params),
            Node(id="g", component="orpheus.builtin.gain", params={"channels": 2}),
            Node(id="sink", component="orpheus.builtin.embed_out", params=io_params),
        ],
        connections=[
            Connection(PortRef.parse("in_pc:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("sink:in")),
        ],
        target="dsp",
    )
    plan = GraphCompiler(registry).compile(project)
    by_id = plan.node_configs
    assert set(by_id) == {"in_dsp", "g", "sink"}
    assert by_id["in_dsp"]["component"] == "orpheus.builtin.embed_in"


def test_loader_roundtrip_alters_and_target(tmp_path, registry) -> None:
    """alters 与 target 经 YAML 保存→重载往返不丢。"""
    project = _project(
        nodes=[
            _node("in_pc", "orpheus.builtin.device_in", alters=["in_dsp"]),
            _node("in_dsp", "orpheus.builtin.embed_in"),
        ],
        connections=[],
        target="dsp",
    )
    path = tmp_path / "project.yaml"
    ProjectLoader().save(project, path)
    loaded = ProjectLoader().load(path)
    assert loaded.target == "dsp"
    assert loaded.graph.nodes["in_pc"].alters == ["in_dsp"]
