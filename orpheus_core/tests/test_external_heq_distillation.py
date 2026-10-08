"""Model_1_2 Headrest/Overhead HEQ executable distillation facts."""

from __future__ import annotations

import json
import re
import struct
from collections import Counter
from pathlib import Path

import yaml
import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]
PROJECT_DIR = ROOT / "examples" / "external_model_heq"


def registry() -> Registry:
    result = Registry([ROOT / "components"])
    result.scan()
    return result


def floats(path: Path) -> list[float]:
    raw = path.read_bytes()
    return list(struct.unpack(f"<{len(raw) // 4}f", raw))


def test_committed_heq_resources_match_generated_default_patterns() -> None:
    assets = PROJECT_DIR / "components" / "heq" / "assets"
    headrest = floats(assets / "headrest_fir.f32")
    overhead = floats(assets / "overhead_fir.f32")
    headrest_iir = floats(assets / "headrest_iir.f32")
    overhead_iir = floats(assets / "overhead_iir.f32")
    assert len(headrest) == 40 * 530
    assert len(overhead) == 20 * 530
    assert len(headrest_iir) == 40 * 140
    assert len(overhead_iir) == 20 * 140
    assert not any(headrest_iir)
    assert not any(overhead_iir)
    assert [(index % 530, value) for index, value in enumerate(headrest) if value] == [
        (529, 1.0)
    ] * 40
    overhead_nonzero = [(index % 530, value) for index, value in enumerate(overhead) if value]
    assert [index for index, _ in overhead_nonzero] == [529] * 20
    assert [value for _, value in overhead_nonzero] == pytest.approx([0.2] * 20)
    headrest_metadata = json.loads((assets / "headrest_metadata.json").read_text(encoding="utf-8"))
    overhead_metadata = json.loads((assets / "overhead_metadata.json").read_text(encoding="utf-8"))
    assert headrest_metadata["input_mapping"] == list(range(10)) * 4
    assert headrest_metadata["output_starts"] == list(range(40))
    assert overhead_metadata["input_mapping"] == [
        0, 2, 4, 6, 8, 1, 3, 5, 7, 9,
        10, 12, 14, 16, 18, 11, 13, 15, 17, 19,
    ]
    assert overhead_metadata["output_starts"] == [0, 5, 10, 15]


def test_heq_directory_project_resolves_resources_and_compiles() -> None:
    base_registry = registry()
    project = ProjectLoader(base_registry).load(PROJECT_DIR)
    assert {sub.id for sub in project.subcomponents} == {
        "external_model.heq.headrest",
        "external_model.heq.overhead",
    }
    assert all(node.position for sub in project.subcomponents for node in sub.graph.nodes.values())
    flat = flatten_project(project)
    headrest_components = Counter(
        node.component for node_id, node in flat.graph.nodes.items()
        if node_id.startswith("headrest__")
    )
    overhead_components = Counter(
        node.component for node_id, node in flat.graph.nodes.items()
        if node_id.startswith("overhead__")
    )
    assert headrest_components == {
        "orpheus.builtin.deinterleave": 1,
        "orpheus.builtin.fir": 40,
        "orpheus.builtin.interleave": 1,
        "orpheus.builtin.delay_line": 1,
    }
    assert overhead_components == {
        "orpheus.builtin.deinterleave": 1,
        "orpheus.builtin.fir": 20,
        "orpheus.builtin.mixer": 16,
        "orpheus.builtin.interleave": 1,
        "orpheus.builtin.delay_line": 1,
    }
    project_registry = project.registry or base_registry
    plan = GraphCompiler(project_registry).compile(flat)
    assert len(plan.node_configs["headrest__fir_00"]["params"]["coefficients"].split(",")) == 530
    assert len(plan.node_configs["overhead__fir_19"]["params"]["coefficients"].split(",")) == 530
    assert plan.node_configs["headrest__join"]["params"]["channels"] == 40
    assert plan.node_configs["headrest__carry"]["params"]["delays_samples"].split(",") == ["2"] * 40
    assert plan.node_configs["overhead__carry"]["params"]["delays_samples"] == "2,2,2,2"
    assert plan.control_links == []


def test_heq_codegen_contains_basic_components_and_headrest_discard(tmp_path: Path) -> None:
    base_registry = registry()
    project = ProjectLoader(base_registry).load(PROJECT_DIR)
    project_registry = project.registry or base_registry
    plan = GraphCompiler(project_registry).compile(flatten_project(project))
    output = tmp_path / "generated"
    CodeGenerator(project_registry, ROOT).generate(plan, output)
    for component in ("fir", "deinterleave", "interleave", "delay_line", "mixer"):
        assert (output / "components" / f"orpheus_builtin_{component}").is_dir()
    graph_source = (output / "src" / "orpheus_graph.c").read_text(encoding="utf-8")
    assert "g_discard_headrest__carry_out" in graph_source
    assert "headrest__fir_39" in graph_source
    assert "overhead__sum_g03" in graph_source
    literals = re.findall(r'"(?:\\.|[^"\\])*"', graph_source)
    assert max(map(len, literals)) <= 1100


def test_heq_project_notes_cover_every_visible_node() -> None:
    document = yaml.safe_load((PROJECT_DIR / "project.yaml").read_text(encoding="utf-8"))
    notes = json.loads((PROJECT_DIR / "node-notes.json").read_text(encoding="utf-8"))
    project = ProjectLoader(registry()).load(PROJECT_DIR)
    expected = {node["id"] for node in document["graph"]["nodes"]}
    expected.update(
        f"sub:{sub.id}/{node.id}"
        for sub in project.subcomponents
        for node in sub.graph.nodes.values()
    )
    assert set(notes) == expected