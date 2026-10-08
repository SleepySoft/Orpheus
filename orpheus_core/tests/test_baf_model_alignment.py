"""BAF generated-model facts retained by the Symphony reference projects."""

from __future__ import annotations

import json
from pathlib import Path

import pytest
import yaml

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.lesson import evaluate_lesson
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def compiler() -> GraphCompiler:
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    return GraphCompiler(registry)


def compile_example(compiler: GraphCompiler, name: str):
    project = ProjectLoader().load(ROOT / "examples" / name)
    return compiler.compile(flatten_project(project))


def test_asm_rnc_uses_generated_model_dimensions(compiler: GraphCompiler) -> None:
    plan = compile_example(compiler, "symphony_asm_ehc_rnc.yaml")
    config = plan.node_configs["rnc_sub__rnc_nlms"]
    assert config["component"] == "orpheus.builtin.rnc_mimo_nlms"
    assert config["params"]["reference_channels"] == 12
    assert config["params"]["output_channels"] == 8
    assert config["params"]["filter_length"] == 125
    assert config["params"]["step_sizes"] == "0,0,0,0,0,0,0,0"
    assert any(
        link["src_node"] == "rnc_sub__slow_probe"
        and link["dst_node"] == "rnc_sub__rnc_gain"
        for link in plan.control_links
    )
    assert sum(1 for buffer in plan.buffers.values() if buffer.get("task_bridge")) >= 8


def test_sas_uses_generated_piecewise_soft_clipper(compiler: GraphCompiler) -> None:
    plan = compile_example(compiler, "symphony_baf_structural_reference.yaml")
    config = plan.node_configs["post_process__sclip"]
    assert config["component"] == "orpheus.builtin.baf_soft_clipper"
    assert config["params"]["xmin"] == pytest.approx(0.65)
    assert config["params"]["xmax"] == pytest.approx(1.35)
    assert config["params"]["p2"] == pytest.approx(0.714285731)


def test_baf_structural_reference_does_not_claim_control_equivalence(
    compiler: GraphCompiler,
) -> None:
    plan = compile_example(compiler, "symphony_baf_structural_reference.yaml")
    assert sum(node.startswith("model_1_2__") for node in plan.node_configs) == 16
    assert plan.control_links == []


def test_baf_structural_reference_declares_fidelity_and_documents_every_node() -> None:
    project_path = ROOT / "examples" / "symphony_baf_structural_reference.yaml"
    notes_path = ROOT / "examples" / "symphony_baf_structural_reference.node-notes.json"
    document = yaml.safe_load(project_path.read_text(encoding="utf-8"))
    notes = json.loads(notes_path.read_text(encoding="utf-8"))
    fidelity = document["model_tree"]["fidelity"]
    assert fidelity["level"] == "structural_reference"
    assert fidelity["executable_equivalence"] is False
    assert fidelity["numerical_equivalence"] is False
    node_ids = {node["id"] for node in document["graph"]["nodes"]}
    node_ids.update(
        f"sub:{subcomponent['id']}/{node['id']}"
        for subcomponent in document.get("subcomponents", [])
        for node in subcomponent["graph"]["nodes"]
    )
    assert set(notes) == node_ids


def test_asm_codegen_allocates_discard_outputs(compiler: GraphCompiler, tmp_path: Path) -> None:
    plan = compile_example(compiler, "symphony_asm_ehc_rnc.yaml")
    output = tmp_path / "generated"
    CodeGenerator(compiler.registry, ROOT).generate(plan, output)
    source = (output / "src" / "orpheus_graph.c").read_text(encoding="utf-8")
    assert "g_discard_rnc_noise_floor__nf_probe_out" in source
    assert "g_discard_rnc_divergence_detector__spkr_probe_out" in source
    assert 'orpheus_graph_fail("process_task:tid5", "rnc_noise_floor__nf_probe"' in source


def test_asm_lesson_checks_pass(compiler: GraphCompiler) -> None:
    project = ProjectLoader().load(ROOT / "examples" / "symphony_asm_ehc_rnc.yaml")
    flat = flatten_project(project)
    plan = compiler.compile(flat)
    result = evaluate_lesson(project, flat, plan)
    assert result["passed"] is True
    assert result["passed_count"] == result["total"] == 5


def test_codegen_c_string_escape_handles_controls() -> None:
    assert CodeGenerator._c_escape('C:\\模型\n"x"\t\b\f\x01') == (
        'C:\\\\模型\\n\\"x\\"\\t\\b\\f\\001'
    )
