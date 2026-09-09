"""视频响度均衡与可选人声增强示例测试。"""

from pathlib import Path

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "video_loudness_voice.yaml"


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
