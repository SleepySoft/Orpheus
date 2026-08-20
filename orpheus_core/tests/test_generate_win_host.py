"""win 实时宿主代码生成测试。

覆盖：含设备组件的图（平台解析为 win）生成 miniaudio 宿主（host_win.c +
orpheus_host_config.h + miniaudio.h + CMake 链接），main.c 让出 main()；
alter 对按 target 选择宿主形态（dsp=platform_io.c 骨架，win=host_win.c）；
平台不可达报错；文件时钟图生成结果不变（回归）；控制层标量 API 落盘。
"""

from __future__ import annotations

from pathlib import Path

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture()
def registry():
    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    return reg


def _node(node_id: str, component: str, alters: list[str] | None = None,
          params: dict | None = None) -> Node:
    # 手工构造的 Node 不经 ProjectLoader 默认值填充，参数（如 channels）需显式给全
    return Node(id=node_id, component=component, alters=list(alters or []),
                params=dict(params or {}))


def _project(nodes: list[Node], connections: list[Connection], target: str = "auto") -> Project:
    project = Project(target=target)
    project.graph = Graph(nodes={n.id: n for n in nodes}, connections=connections)
    return project


def _device_project(target: str = "auto") -> Project:
    return _project(
        nodes=[
            _node("dev_in", "orpheus.builtin.device_in", params={"channels": 2}),
            _node("g", "orpheus.builtin.gain", params={"channels": 2}),
            _node("dev_out", "orpheus.builtin.device_out", params={"channels": 2}),
        ],
        connections=[
            Connection(PortRef.parse("dev_in:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("dev_out:in")),
        ],
        target=target,
    )


def _alter_project(target: str = "auto") -> Project:
    """device_in(win) 与 embed_in(dsp) 互为 alter 的输入链。"""
    return _project(
        nodes=[
            _node("in_pc", "orpheus.builtin.device_in", alters=["in_dsp"],
                  params={"channels": 2}),
            _node("in_dsp", "orpheus.builtin.embed_in", params={"channels": 2}),
            _node("g", "orpheus.builtin.gain", params={"channels": 2}),
            _node("out", "orpheus.builtin.wav_out",
                  params={"channels": 2, "file_path": "out.wav"}),
        ],
        connections=[
            Connection(PortRef.parse("in_pc:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("out:in")),
        ],
        target=target,
    )


def _generate(registry, project: Project, out_dir: Path, target: str | None = None):
    plan = GraphCompiler(registry).compile(project, target=target)
    CodeGenerator(registry, ROOT).generate(plan, out_dir)
    return plan


def test_device_graph_generates_win_host(registry, tmp_path) -> None:
    """含设备组件的图（auto → win）生成 miniaudio 实时宿主，main.c 让出 main()。"""
    plan = _generate(registry, _device_project(), tmp_path)
    assert plan.target == "win"

    host = (tmp_path / "src" / "host_win.c").read_text(encoding="utf-8")
    assert "int main(void)" in host
    assert "ma_device_init" in host

    cfg = (tmp_path / "include" / "orpheus_host_config.h").read_text(encoding="utf-8")
    assert "#define ORPHEUS_HOST_HAS_IN 1" in cfg
    assert "#define ORPHEUS_HOST_HAS_OUT 1" in cfg
    assert "#define ORPHEUS_HOST_LOOPBACK 0" in cfg
    assert f"#define ORPHEUS_HOST_SAMPLE_RATE {plan.sample_rate}u" in cfg
    assert f"#define ORPHEUS_HOST_BLOCK_SIZE {plan.block_size}u" in cfg

    assert (tmp_path / "include" / "miniaudio.h").is_file()

    main_c = (tmp_path / "src" / "main.c").read_text(encoding="utf-8")
    assert "int main(" not in main_c  # main() 在 host_win.c（设备时钟）
    assert "OrpheusBuffer* orpheus_host_device_in_buffer(void)" in main_c
    assert "OrpheusBuffer* orpheus_host_device_out_buffer(void)" in main_c
    assert "void orpheus_generated_teardown(void)" in main_c
    assert "int orpheus_generated_process(uint32_t frame_count) {" in main_c
    assert "static int orpheus_generated_process" not in main_c

    gen_h = (tmp_path / "include" / "orpheus_generated.h").read_text(encoding="utf-8")
    assert "orpheus_host_device_in_buffer" in gen_h

    cmake = (tmp_path / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "src/host_win.c" in cmake
    assert "winmm" in cmake


def test_device_loopback_and_device_params_in_host_config(registry, tmp_path) -> None:
    """设备参数（loopback/设备名/通道数）注入 orpheus_host_config.h。"""
    project = _project(
        nodes=[
            _node("dev_in", "orpheus.builtin.device_in",
                  params={"source": "loopback", "channels": 2, "device": "Speaker"}),
            _node("dev_out", "orpheus.builtin.device_out",
                  params={"channels": 2, "device": "Headphone"}),
        ],
        connections=[Connection(PortRef.parse("dev_in:out"), PortRef.parse("dev_out:in"))],
    )
    _generate(registry, project, tmp_path)
    cfg = (tmp_path / "include" / "orpheus_host_config.h").read_text(encoding="utf-8")
    assert "#define ORPHEUS_HOST_LOOPBACK 1" in cfg
    assert '#define ORPHEUS_HOST_IN_DEVICE "Speaker"' in cfg
    assert '#define ORPHEUS_HOST_OUT_DEVICE "Headphone"' in cfg


def test_alter_pair_target_dsp_keeps_embed_skeleton(registry, tmp_path) -> None:
    """alter 对 + target=dsp：激活 embed_in，生成嵌入骨架，不产生 win 宿主。"""
    plan = _generate(registry, _alter_project(target="dsp"), tmp_path)
    assert plan.target == "dsp"
    assert (tmp_path / "src" / "platform_io.c").is_file()
    assert not (tmp_path / "src" / "host_win.c").exists()
    main_c = (tmp_path / "src" / "main.c").read_text(encoding="utf-8")
    assert "int main(" in main_c  # 文件时钟缺省宿主保留


def test_alter_pair_target_win_generates_win_host(registry, tmp_path) -> None:
    """alter 对 + target=win：激活 device_in，生成 win 实时宿主。"""
    plan = _generate(registry, _alter_project(target="win"), tmp_path)
    assert plan.target == "win"
    assert (tmp_path / "src" / "host_win.c").is_file()
    assert not (tmp_path / "src" / "platform_io.c").exists()


def test_alter_pair_auto_prefers_win(registry, tmp_path) -> None:
    """alter 对 + auto：win 优先，生成 win 实时宿主。"""
    plan = _generate(registry, _alter_project(), tmp_path)
    assert plan.target == "win"
    assert (tmp_path / "src" / "host_win.c").is_file()


def test_dsp_target_unreachable_without_alter(registry, tmp_path) -> None:
    """设备组件无 alter 且 target=dsp：平台不可达，编译报错并列出断点。"""
    with pytest.raises(CompileError, match="dsp"):
        GraphCompiler(registry).compile(_device_project(target="dsp"))


def test_control_scalar_api_generated(registry, tmp_path) -> None:
    """控制层产出标量读写/探针枚举/按 ID 读写 API（win 宿主控制面依赖）。"""
    _generate(registry, _device_project(), tmp_path)
    ctrl = (tmp_path / "src" / "orpheus_control.c").read_text(encoding="utf-8")
    for sym in ("orpheus_control_set_value(", "orpheus_control_get_value(",
                "orpheus_control_probe_count(", "orpheus_control_probe_get(",
                "orpheus_control_set_value_id(", "orpheus_control_get_value_id("):
        assert sym in ctrl
    hdr = (tmp_path / "include" / "orpheus_control.h").read_text(encoding="utf-8")
    assert "orpheus_control_probe_get" in hdr


def test_id_map_entries_carry_node_and_key(registry, tmp_path) -> None:
    """OrpheusIdEntry 带 node/key（宿主 RESOLVE/MAP 与 GETBULK 反查依赖）。"""
    _generate(registry, _device_project(), tmp_path)
    id_map = (tmp_path / "src" / "orpheus_id_map.c").read_text(encoding="utf-8")
    assert '"g", "gain_db"' in id_map  # 叶子数据点带节点与槽 key


def test_file_graph_unchanged(registry, tmp_path) -> None:
    """回归：文件时钟图（无设备组件）生成结果不变——文件宿主 main() 保留。"""
    project = _project(
        nodes=[
            _node("src", "orpheus.builtin.wav_in",
                  params={"channels": 2, "file_path": "in.wav"}),
            _node("g", "orpheus.builtin.gain", params={"channels": 2}),
            _node("out", "orpheus.builtin.wav_out",
                  params={"channels": 2, "file_path": "out.wav"}),
        ],
        connections=[
            Connection(PortRef.parse("src:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("out:in")),
        ],
    )
    _generate(registry, project, tmp_path)
    assert not (tmp_path / "src" / "host_win.c").exists()
    assert not (tmp_path / "include" / "orpheus_host_config.h").exists()
    main_c = (tmp_path / "src" / "main.c").read_text(encoding="utf-8")
    assert "int main(" in main_c
    cmake = (tmp_path / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "winmm" not in cmake
