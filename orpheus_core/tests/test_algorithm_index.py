"""Complete Algorithm IR and generated full-model projection tests."""

from __future__ import annotations

import copy
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

from orpheus_core.algorithm_index import (
    AlgorithmIndexError,
    emit_structural_project,
    load_algorithm_index,
    validate_algorithm_index,
    verify_source_fingerprints,
)
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.server.manager import ProjectManager
from orpheus_core.subgraph import flatten_project
from orpheus_core.validation import validate_project


ROOT = Path(__file__).resolve().parents[2]
INDEX = ROOT / "examples" / "recycled" / "external_model_structural_reference" / "algorithm-index.yaml"
FULL_PROJECT = ROOT / "examples" / "external_model_full"
_SOURCE_ROOT_VALUE = os.environ.get("ORPHEUS_EXTERNAL_MODEL_ROOT")
SOURCE_ROOT = Path(_SOURCE_ROOT_VALUE) if _SOURCE_ROOT_VALUE else None


def builtin_registry() -> Registry:
    registry = Registry([ROOT / "components"])
    registry.scan()
    return registry


def tree_bytes(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def test_full_algorithm_index_has_closed_hierarchy_and_source_schedule() -> None:
    document = load_algorithm_index(INDEX)
    definitions = {item["id"]: item for item in document["definitions"]}
    assert len(definitions) == 68
    assert len(document["tasks"]) == 11
    assert document["system"]["roots"] == ["A.RUNTIME", "B.RUNTIME"]
    assert len(document["sources"]) == 29
    assert all(len(source["sha256"]) == 64 for source in document["sources"])
    assert len(document["parameter_partitions"]) == 17
    assert sum(item["field_count"] for item in document["parameter_partitions"]) == 404
    assert {
        owner
        for partition in document["parameter_partitions"]
        for owner in partition["owner_refs"]
    } <= set(definitions)
    assert set(document["coverage"]["definition_ids"]) == set(definitions)
    assert definitions["A.PREAMP"]["children"] == [
        "A.PREAMP.INPUT_MIXER",
        "A.PREAMP.MUSIC_VOLUME",
        "A.PREAMP.BASS",
        "A.PREAMP.TREBLE",
        "A.PREAMP.MID",
        "A.PREAMP.BALANCE",
        "A.PREAMP.ROUTE_LEVEL",
    ]
    a0 = next(task for task in document["tasks"] if task["id"] == "A0")
    b3 = next(task for task in document["tasks"] if task["id"] == "B3")
    assert a0["period_ticks"] == 1
    assert b3["period_ticks"] == 13
    assert b3["ordinal_calls"] == [
        "B.BANDS",
        "B.FDP",
        "B.MIX_CONTROL",
        "B.MIX_DECI",
        "B.HEQ",
        "B.POST",
    ]


def test_index_rejects_duplicate_yaml_keys(tmp_path: Path) -> None:
    source = INDEX.read_text(encoding="utf-8")
    duplicate = source.replace(
        "model_id: external_model.dual_runtime_audio",
        "model_id: external_model.dual_runtime_audio\nmodel_id: external_model.duplicate",
        1,
    )
    path = tmp_path / "duplicate.yaml"
    path.write_text(duplicate, encoding="utf-8")
    with pytest.raises(AlgorithmIndexError, match="duplicate YAML key"):
        load_algorithm_index(path)


def test_index_rejects_bus_overlap_coverage_gap_and_edge_type_mismatch() -> None:
    document = load_algorithm_index(INDEX)
    overlap = copy.deepcopy(document)
    overlap["buses"][0]["fields"][1]["offset_words"] = 0
    with pytest.raises(AlgorithmIndexError, match="overlapping field"):
        validate_algorithm_index(overlap)

    coverage_gap = copy.deepcopy(document)
    coverage_gap["coverage"]["definition_ids"].pop()
    with pytest.raises(AlgorithmIndexError, match="coverage mismatch"):
        validate_algorithm_index(coverage_gap)

    type_mismatch = copy.deepcopy(document)
    runtime_a = next(item for item in type_mismatch["definitions"] if item["id"] == "A.RUNTIME")
    runtime_a["body"]["edges"][0]["type_ref"] = "audio1"
    with pytest.raises(AlgorithmIndexError, match="edge type mismatch"):
        validate_algorithm_index(type_mismatch)


def test_full_index_preserves_critical_corrections_and_parallel_boundaries() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    assert definitions["B.FDP"]["interface"]["outputs"][0]["type_ref"] == "audio8"
    assert "八路" in definitions["B.FDP.WOLA"]["purpose"]
    assert any("34-sample" in operation for operation in definitions["B.HEQ.OVERHEAD"]["operations"])
    assert definitions["B.HEQ"]["children"] == [
        "B.HEQ.PERIPHERAL",
        "B.HEQ.HEADREST",
        "B.HEQ.OVERHEAD",
    ]
    overhead_blockers = definitions["B.HEQ.OVERHEAD"]["implementation"]["blockers"]
    assert any("34 样本" in item["reason"] for item in overhead_blockers)
    assert definitions["A.PREAMP.ROUTE_LEVEL"]["operations"][0].startswith("Volume")


def test_full_index_bus_offsets_cover_exact_typed_transactions() -> None:
    document = load_algorithm_index(INDEX)
    types = {item["id"]: item for item in document["types"]}
    buses = {item["id"]: item for item in document["buses"]}
    assert buses["bus_a_to_b"]["total_words"] == 488
    assert buses["bus_b_to_a"]["total_words"] == 1193
    for bus in buses.values():
        occupied: list[int] = []
        for field in bus["fields"]:
            words = 1
            for dimension in types[field["type_ref"]]["shape"]:
                words *= dimension
            occupied.extend(range(field["offset_words"], field["offset_words"] + words))
        assert occupied == list(range(bus["total_words"]))


def test_full_index_builder_is_reproducible(tmp_path: Path) -> None:
    output = tmp_path / "algorithm-index.yaml"
    subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "build_external_full_index.py"), str(output)],
        cwd=ROOT,
        check=True,
    )
    assert output.read_bytes() == INDEX.read_bytes()


def test_full_index_source_fingerprints_when_external_source_is_available() -> None:
    if SOURCE_ROOT is None or not SOURCE_ROOT.is_dir():
        return
    verify_source_fingerprints(load_algorithm_index(INDEX), SOURCE_ROOT)


def test_structural_projection_is_deterministic_and_self_contained(tmp_path: Path) -> None:
    document = load_algorithm_index(INDEX)
    first = tmp_path / "first"
    second = tmp_path / "second"
    emit_structural_project(document, first)
    emit_structural_project(document, second)
    assert tree_bytes(first) == tree_bytes(second)
    assert (first / "algorithm-index.yaml").read_bytes() == INDEX.read_bytes()
    assert len(list((first / "components").glob("*.component.yaml"))) == 68
    project = yaml.safe_load((first / "project.yaml").read_text(encoding="utf-8"))
    projection = project["algorithm_projection"]
    assert projection["definition_count"] == 68
    assert len(projection["tasks"]) == 11
    assert len(projection["parameter_partitions"]) == 17
    assert projection["unlowered_edges"]
    report = json.loads((first / "coverage-report.json").read_text(encoding="utf-8"))
    assert report["definitions"] == 68
    assert report["leaves"] + report["composites"] == 68
    assert report["executable"] is False
    assert report["blocker_gates"]["golden"] == report["leaves"]


def test_projection_refuses_to_overwrite_unmarked_directory(tmp_path: Path) -> None:
    output = tmp_path / "unrelated"
    output.mkdir()
    (output / "keep.txt").write_text("user data", encoding="utf-8")
    with pytest.raises(AlgorithmIndexError, match="refusing to overwrite"):
        emit_structural_project(load_algorithm_index(INDEX), output)
    assert (output / "keep.txt").read_text(encoding="utf-8") == "user data"


def test_committed_full_project_loads_recursively_and_notes_every_visible_node() -> None:
    base_registry = builtin_registry()
    project = ProjectLoader(base_registry).load(FULL_PROJECT)
    project_registry = project.registry or base_registry
    assert len(project_registry.list_components()) >= 68
    assert {node.component for node in project.graph.nodes.values()} == {
        "external_model.index.a_runtime",
        "external_model.index.b_runtime",
    }
    assert {
        (str(connection.from_ref), str(connection.to_ref))
        for connection in project.graph.connections
    } == {
        ("a_runtime:bus_to_b", "b_runtime:bus_from_a"),
        ("b_runtime:bus_to_a", "a_runtime:bus_from_b"),
    }
    notes = json.loads((FULL_PROJECT / "node-notes.json").read_text(encoding="utf-8"))
    expected = set(project.graph.nodes)
    expected.update(
        f"sub:{sub.id}/{node.id}"
        for sub in project.subcomponents
        for node in sub.graph.nodes.values()
    )
    assert set(notes) == expected
    flat = flatten_project(project)
    assert any(
        project_registry.get(node.component).package_type == "placeholder"
        for node in flat.graph.nodes.values()
    )
    with pytest.raises(CompileError, match="未实现算法组件"):
        GraphCompiler(project_registry).compile(flat)

    report = validate_project(FULL_PROJECT / "project.yaml", base_registry)
    assert report.valid
    assert report.stages["compile"] == "blocked"
    assert any("placeholder" in issue.message for issue in report.issues)


def test_committed_projection_matches_fresh_generation(tmp_path: Path) -> None:
    output = tmp_path / "projection"
    emit_structural_project(load_algorithm_index(INDEX), output)
    assert tree_bytes(output) == tree_bytes(FULL_PROJECT)


def test_full_research_graph_is_published_but_evidence_project_is_hidden() -> None:
    examples = ProjectManager(ROOT).list_examples()
    assert "external_model_full" in examples
    assert "external_model_structural_reference" not in examples
