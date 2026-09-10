"""视频响度均衡与可选人声增强示例测试。"""

from pathlib import Path

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "video_loudness_voice.yaml"
DECOMPOSED_EXAMPLE = ROOT / "examples" / "video_loudness_voice_decomposed.yaml"


def test_video_loudness_example_compiles_and_generates(tmp_path) -> None:
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = ProjectLoader().load(EXAMPLE)

    plan = GraphCompiler(registry).compile(project, target="win")
    assert plan.target == "win"
    assert plan.node_configs["leveler"]["component"] == "orpheus.builtin.loudness_normalizer"
    assert plan.node_configs["voice_select"]["params"]["select"] == 1
    assert plan.node_configs["voice_select"]["params"]["ramp_ms"] == 30.0
    endpoints = {(edge["from"], edge["to"]) for edge in plan.connections}
    assert ("voice_off:out", "voice_select:in0") in endpoints
    assert ("voice_clarity:out", "voice_select:in1") in endpoints

    CodeGenerator(registry, ROOT).generate(plan, tmp_path)
    graph_source = (tmp_path / "src" / "orpheus_graph.c").read_text(encoding="utf-8")
    cmake = (tmp_path / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "orpheus_builtin_loudness_normalizer_get_interface" in graph_source
    assert "target_link_libraries(orpheus_graph PUBLIC m)" in cmake
    assert "target_link_libraries(orpheus_generated_app PRIVATE ole32 oleaut32 uuid winmm)" in cmake


def test_decomposed_loudness_example_exposes_control_stages(tmp_path) -> None:
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = ProjectLoader().load(DECOMPOSED_EXAMPLE)

    plan = GraphCompiler(registry).compile(project, target="win")
    assert plan.node_configs["rms_detector"]["component"] == "orpheus.builtin.probe_rms"
    assert plan.node_configs["gain_controller"]["component"] == "orpheus.builtin.loudness_gain_control"
    assert plan.node_configs["auto_gain"]["component"] == "orpheus.builtin.gain"
    assert plan.node_configs["auto_gain"]["params"]["smoothing_ms"] == 0.0
    assert [(link["src_node"], link["src_param"], link["dst_node"], link["dst_param"])
            for link in plan.control_links] == [
        ("rms_detector", "rms", "gain_controller", "level"),
        ("gain_controller", "gain_db", "auto_gain", "gain_db"),
    ]

    CodeGenerator(registry, ROOT).generate(plan, tmp_path)
    graph_source = (tmp_path / "src" / "orpheus_graph.c").read_text(encoding="utf-8")
    assert "static void control_tick(void)" in graph_source
    assert "rms_detector.rms [标量] -> gain_controller.level" in graph_source
    assert "gain_controller.gain_db [标量] -> auto_gain.gain_db" in graph_source
