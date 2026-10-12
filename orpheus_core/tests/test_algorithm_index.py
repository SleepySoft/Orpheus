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
MANUAL_PROJECT_FILES = {"TODO.md"}
MANUAL_PROJECT_PREFIXES = ("analysis/",)


def builtin_registry() -> Registry:
    registry = Registry([ROOT / "components"])
    registry.scan()
    return registry


def tree_bytes(root: Path, exclude: set[str] | None = None) -> dict[str, bytes]:
    excluded = exclude or set()
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
        and path.relative_to(root).as_posix() not in excluded
        and not path.relative_to(root).as_posix().startswith(MANUAL_PROJECT_PREFIXES)
    }


def test_full_algorithm_index_has_closed_hierarchy_and_source_schedule() -> None:
    document = load_algorithm_index(INDEX)
    definitions = {item["id"]: item for item in document["definitions"]}
    assert len(definitions) == 67
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
    io_projection = document["system"]["io_projection"]
    assert [item["id"] for item in io_projection] == ["program_input", "main_output"]
    assert io_projection[0]["type_ref"] == "audio2"
    assert io_projection[0]["adapter"]["output_type_ref"] == "audio30"
    assert io_projection[1]["type_ref"] == "audio22"


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


def test_b_bands_mono_verified_lowering_matches_default_basis_vectors() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    implementation = definitions["B.BANDS.MONO"]["implementation"]
    assert implementation["status"] == "verified"
    assert implementation["blockers"] == []
    lowering = implementation["lowering"]
    assert lowering["component"] == "orpheus.builtin.matrix_mul"
    params = lowering["params"]
    matrix = [float(value.strip()) for value in params["matrix"].split(",")]
    assert params["rows"] == 1
    assert params["cols"] == 10
    assert matrix == [0.5, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    for channel, expected in enumerate(matrix):
        basis = [0.0] * params["cols"]
        basis[channel] = 1.0
        actual = sum(coefficient * sample for coefficient, sample in zip(matrix, basis))
        assert actual == expected


def test_a_preamp_input_mixer_verified_lowering_matches_both_basis_matrices() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    implementation = definitions["A.PREAMP.INPUT_MIXER"]["implementation"]
    assert implementation["status"] == "verified"
    assert implementation["blockers"] == []
    lowering = implementation["lowering"]
    nodes = {node["id"]: node for node in lowering["nodes"]}
    assert nodes["mux"]["params"]["select_index"] == 0
    assert lowering["parameters"][0]["maps_to"] == "mux:select_index"

    matrix_514 = [float(value.strip()) for value in nodes["mix_514"]["params"]["weights"].split(",")]
    matrix_stereo = [float(value.strip()) for value in nodes["mix_stereo"]["params"]["weights"].split(",")]
    assert len(matrix_514) == len(matrix_stereo) == 120
    expected_514 = {
        0: {0: 1.0}, 1: {1: 1.0}, 2: {2: 1.0}, 3: {3: 3.16},
        4: {4: 1.0, 6: 1.0}, 5: {5: 1.0, 7: 1.0},
        6: {8: 1.0}, 7: {9: 1.0}, 8: {10: 1.0}, 9: {11: 1.0},
    }
    expected_stereo = {
        0: {0: 1.0, 2: 0.5, 3: 1.58, 4: 1.0, 6: 1.0, 8: 1.0, 9: 1.0},
        1: {1: 1.0, 2: 0.5, 3: 1.58, 5: 1.0, 7: 1.0, 10: 1.0, 11: 1.0},
    }
    for matrix, expected in ((matrix_514, expected_514), (matrix_stereo, expected_stereo)):
        for output in range(10):
            row = matrix[output * 12:(output + 1) * 12]
            assert {channel: gain for channel, gain in enumerate(row) if gain != 0.0} == expected.get(output, {})


def test_b_mix_deci_organize_and_route_match_source_permutations() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    organize = definitions["B.MIX_DECI.ORGANIZE"]
    assert [port["type_ref"] for port in organize["interface"]["outputs"]] == ["audio17", "audio21"]
    organize_nodes = {node["id"]: node for node in organize["implementation"]["lowering"]["nodes"]}
    assert organize_nodes["cs"]["params"]["indices"] == "0,2,4,6,8,15,17,18,1,3,5,7,9,16,19,20,14"
    assert organize_nodes["lr"]["params"]["indices"] == "0,2,4,6,8,10,12,15,17,18,1,3,5,7,9,11,13,16,19,20,14"

    ramp = definitions["B.MIX_DECI.RAMP"]
    assert [port["type_ref"] for port in ramp["interface"]["inputs"]] == ["audio17", "audio21", "gains1134"]
    assert [port["type_ref"] for port in ramp["interface"]["outputs"]] == ["audio2", "audio25", "audio25", "gains1134"]

    route = definitions["B.MIX_DECI.ROUTE"]["implementation"]["lowering"]
    mapping = {connection["from"]: connection["to"] for connection in route["connections"]}
    assert len(mapping) == 52
    for index in range(2):
        assert mapping[f"center:out{index}"] == f"peripheral:in{index}"
    for index in range(10):
        assert mapping[f"left:out{index}"] == f"peripheral:in{index + 2}"
        assert mapping[f"right:out{index}"] == f"peripheral:in{index + 12}"
        assert mapping[f"left:out{index + 10}"] == f"overhead:in{index}"
        assert mapping[f"right:out{index + 10}"] == f"overhead:in{index + 10}"
    for index in range(5):
        assert mapping[f"left:out{index + 20}"] == f"headrest:in{index * 2}"
        assert mapping[f"right:out{index + 20}"] == f"headrest:in{index * 2 + 1}"


def test_a_input_select_shared_route_then_three_selectors() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    definition = definitions["A.INPUT_SELECT"]
    assert definition["implementation"]["status"] == "verified"
    lowering = definition["implementation"]["lowering"]
    nodes = {node["id"]: node for node in lowering["nodes"]}
    assert nodes["route"]["params"]["select"] == ",".join(str(index) for index in range(1, 31))
    assert nodes["entertainment"]["params"]["select"] == ",".join(str(index) for index in range(1, 13))
    assert nodes["microphone"]["params"]["select"] == "30"
    assert nodes["announcement"]["params"]["select"] == ",".join(str(index) for index in range(13, 30))
    connections = {(item["from"], item["to"]) for item in lowering["connections"]}
    assert connections == {
        ("ingress:out", "route:in"),
        ("route:out", "entertainment:in"),
        ("route:out", "microphone:in"),
        ("route:out", "announcement:in"),
    }
    public = lowering["parameters"][0]
    assert public["maps_to"] == "route:override_select"
    assert public["default"] == ",".join("0" for _ in range(30))


def test_a_preamp_balance_uses_exact_shared_ramper_bank_policy() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    definition = definitions["A.PREAMP.BALANCE"]
    implementation = definition["implementation"]
    assert implementation["status"] == "verified"
    assert implementation["blockers"] == []
    lowering = implementation["lowering"]
    assert lowering["component"] == "orpheus.builtin.sleeping_beauty"
    params = lowering["params"]
    assert params["channels"] == 10
    assert params["center_mono_mode"] == "mean_lr"
    assert params["chan_map"] == "0,1,2,3,0,1,0,0,1,1"
    assert len(params["table_idx"].split(",")) == 30
    assert len(params["table_db"].split(",")) == 30
    assert {parameter["id"] for parameter in lowering["parameters"]} == {
        "gain_index", "ramp_db_per_s", "ramp_ms", "freeze",
    }


def test_a_preamp_tone_leaves_use_exact_indexed_tone_coefficients() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    expected = {
        "A.PREAMP.BASS": (1, [0.00779292, 0.00779292, 0.0, -0.98441416, 0.0]),
        "A.PREAMP.TREBLE": (1, [0.788675129, -0.788675129, 0.0, -0.577350259, 0.0]),
        "A.PREAMP.MID": (2, [0.0532660671, 0.0, -0.0532660671, -1.88373291, 0.893467903]),
    }
    for definition_id, (order, coefficients) in expected.items():
        implementation = definitions[definition_id]["implementation"]
        assert implementation["status"] == "verified"
        assert implementation["blockers"] == []
        lowering = implementation["lowering"]
        assert lowering["component"] == "orpheus.builtin.indexed_tone"
        params = lowering["params"]
        assert params["filter_order"] == order
        actual = [float(value.strip()) for value in params["boost_coefs"].split(",")]
        assert actual == pytest.approx(coefficients)
        assert params["cut_coefs"] == params["boost_coefs"]


def test_a_preamp_music_volume_preserves_delay_table_and_cold_start() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    implementation = definitions["A.PREAMP.MUSIC_VOLUME"]["implementation"]
    assert implementation["status"] == "verified"
    nodes = {node["id"]: node for node in implementation["lowering"]["nodes"]}
    assert nodes["delay"]["params"] == {
        "channels": 10,
        "max_delay_samples": 72,
        "delays_samples": ",".join("72" for _ in range(10)),
    }
    gain = nodes["gain"]["params"]
    assert gain["initial_gain"] == 0.04
    assert gain["apply_index_on_prepare"] is False
    assert gain["table_idx"] == "0,111,176,200,255,255,255,255"
    assert gain["table_db"] == "-128,-72,-39.5,-27.5,0,0,0,0"


def test_b_post_sleeping_beauty_preserves_source_boundary_and_cold_start() -> None:
    document = load_algorithm_index(INDEX)
    definitions = {item["id"]: item for item in document["definitions"]}
    implementation = definitions["B.POST.SLEEPING"]["implementation"]
    assert implementation["status"] == "verified"
    assert implementation["blockers"] == []
    interface = definitions["B.POST.SLEEPING"]["interface"]
    assert [item["id"] for item in interface["outputs"]] == [
        "peripheral_out", "overhead_out", "headrest_out",
    ]

    lowering = implementation["lowering"]
    nodes = {node["id"]: node for node in lowering["nodes"]}
    gain = nodes["gain"]["params"]
    assert gain["channels"] == 54
    assert gain["apply_index_on_prepare"] is False
    assert gain["center_mono_mode"] == "attenuated"
    assert gain["mutes_bass"] == 0.0
    assert [int(value) for value in gain["chan_map"].split(",")] == [
        2, 2, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    ]
    assert len(gain["table_idx"].split(",")) == 30
    assert len(gain["table_db"].split(",")) == 30
    assert len(lowering["connections"]) == 110

    split = definitions["B.POST.HEADREST_SPLIT"]["implementation"]
    assert split["status"] == "verified"
    assert len(split["lowering"]["connections"]) == 40
    types = {item["id"]: item for item in document["types"]}
    assert types["b3_audio54"]["shape"] == [54, 32]
    assert types["b3_audio54"]["sample_rate_hz"] == pytest.approx(11076.923076923076)


def test_a_post_hologram_preserves_unity_start_reset_and_zero_phase_delay() -> None:
    definitions = {
        item["id"]: item for item in load_algorithm_index(INDEX)["definitions"]
    }
    implementation = definitions["A.SPATIAL_FULL.POST_HOLOGRAM"]["implementation"]
    assert implementation["status"] == "verified"
    assert implementation["blockers"] == []
    nodes = {node["id"]: node for node in implementation["lowering"]["nodes"]}
    gain = nodes["gain"]["params"]
    assert gain["channels"] == 22
    assert gain["apply_index_on_prepare"] is False
    assert gain["preserve_state_on_reset"] is True
    assert gain["chan_map"] == "2,2,0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1,0,0,1,1"
    delay = nodes["phase_delay"]["params"]
    assert delay == {
        "channels": 22,
        "max_delay_samples": 48,
        "delays_samples": ",".join("0" for _ in range(22)),
    }


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


def test_output_io_adapter_is_validated_and_projected_in_signal_order(tmp_path: Path) -> None:
    document = load_algorithm_index(INDEX)
    output = next(item for item in document["system"]["io_projection"] if item["direction"] == "output")
    output["type_ref"] = "audio2"
    output["params"] = {"channels": 2}
    output["adapter"] = {
        "id": "system_to_program",
        "component": "orpheus.builtin.output_router",
        "params": {"input_channels": 22, "output_channels": 2},
        "output_type_ref": "audio2",
    }
    validate_algorithm_index(document)

    projection = tmp_path / "projection"
    emit_structural_project(document, projection)
    project = yaml.safe_load((projection / "project.yaml").read_text(encoding="utf-8"))
    assert {node["id"] for node in project["graph"]["nodes"]} >= {"system_to_program", "main_output"}
    assert project["graph"]["connections"][-2:] == [
        {"from": "a_runtime:main_out", "to": "system_to_program:in"},
        {"from": "system_to_program:out", "to": "main_output:in"},
    ]

    output["adapter"]["output_type_ref"] = "audio1"
    with pytest.raises(AlgorithmIndexError, match="I/O projection type mismatch"):
        validate_algorithm_index(document)


def test_structural_projection_is_deterministic_and_self_contained(tmp_path: Path) -> None:
    document = load_algorithm_index(INDEX)
    first = tmp_path / "first"
    second = tmp_path / "second"
    emit_structural_project(document, first)
    emit_structural_project(document, second)
    assert tree_bytes(first) == tree_bytes(second)
    assert (first / "algorithm-index.yaml").read_bytes() == INDEX.read_bytes()
    assert len(list((first / "components").glob("*.component.yaml"))) == 67
    project = yaml.safe_load((first / "project.yaml").read_text(encoding="utf-8"))
    projection = project["algorithm_projection"]
    assert projection["definition_count"] == 67
    assert projection["types"] == document["types"]
    assert len(projection["tasks"]) == 11
    assert len(projection["parameter_partitions"]) == 17
    assert projection["unlowered_edges"]
    report = json.loads((first / "coverage-report.json").read_text(encoding="utf-8"))
    assert report["definitions"] == 67
    assert report["leaves"] + report["composites"] == 67
    assert report["executable"] is False
    assert report["blocker_gates"]["golden"] == report["leaves"] - 13


def test_projection_refuses_to_overwrite_unmarked_directory(tmp_path: Path) -> None:
    output = tmp_path / "unrelated"
    output.mkdir()
    (output / "keep.txt").write_text("user data", encoding="utf-8")
    with pytest.raises(AlgorithmIndexError, match="refusing to overwrite"):
        emit_structural_project(load_algorithm_index(INDEX), output)
    assert (output / "keep.txt").read_text(encoding="utf-8") == "user data"


def test_projection_preserves_manual_handoff_files(tmp_path: Path) -> None:
    output = tmp_path / "projection"
    emit_structural_project(load_algorithm_index(INDEX), output)
    analysis = output / "analysis" / "distillation-process.md"
    analysis.parent.mkdir()
    analysis.write_text("analysis handoff", encoding="utf-8")
    todo = output / "TODO.md"
    todo.write_text("todo handoff", encoding="utf-8")
    emit_structural_project(load_algorithm_index(INDEX), output)
    assert analysis.read_text(encoding="utf-8") == "analysis handoff"
    assert todo.read_text(encoding="utf-8") == "todo handoff"


def test_committed_full_project_loads_recursively_and_notes_every_visible_node() -> None:
    base_registry = builtin_registry()
    project = ProjectLoader(base_registry).load(FULL_PROJECT)
    project_registry = project.registry or base_registry
    assert len(project_registry.list_components()) >= 66
    assert set(project.graph.nodes) == {
        "a_runtime",
        "b_runtime",
        "program_input",
        "program_to_system",
        "main_output",
    }
    assert {
        (str(connection.from_ref), str(connection.to_ref))
        for connection in project.graph.connections
    } == {
        ("a_runtime:adaptive_audio", "b_runtime:adaptive_audio"),
        ("b_runtime:mono", "a_runtime:mono"),
        ("b_runtime:treble_lr", "a_runtime:treble_lr"),
        ("b_runtime:treble_surround", "a_runtime:treble_surround"),
        ("b_runtime:post_spatial", "a_runtime:post_spatial"),
        ("program_input:out", "program_to_system:in"),
        ("program_to_system:out", "a_runtime:audio_in"),
        ("a_runtime:main_out", "main_output:in"),
    }
    assert project_registry.get("external_model.index.a_bus_exchange") is None
    assert project_registry.get("external_model.index.b_bus_exchange") is None
    mono = project_registry.get("external_model.index.b_bands_mono")
    assert mono.package_type == "composite"
    input_mixer = project_registry.get("external_model.index.a_preamp_input_mixer")
    assert input_mixer.package_type == "composite"
    notes = json.loads((FULL_PROJECT / "node-notes.json").read_text(encoding="utf-8"))
    assert "标准音频端口并写入 graph.connections" in notes["a_runtime"]
    assert "标准 control_connections 映射前只保留" in notes["b_runtime"]
    expected = set(project.graph.nodes)
    expected.update(
        f"sub:{sub.id}/{node.id}"
        for sub in project.subcomponents
        for node in sub.graph.nodes.values()
    )
    assert set(notes) == expected
    flat = flatten_project(project)
    mono_impl = next(node for node in flat.graph.nodes.values() if node.id.endswith("bands__mono__impl"))
    assert mono_impl.component == "orpheus.builtin.matrix_mul"
    assert mono_impl.params == {
        "rows": 1,
        "cols": 10,
        "matrix": "0.5, 0.5, 0, 0, 0, 0, 0, 0, 0, 0",
    }
    input_mixer_nodes = {
        node.id.rsplit("__", 1)[-1]: node
        for node in flat.graph.nodes.values()
        if "input_mixer__" in node.id
    }
    assert {node.component for node in input_mixer_nodes.values()} == {
        "orpheus.builtin.gain",
        "orpheus.builtin.input_mixer_3d",
        "orpheus.builtin.n_way_mux",
    }
    assert input_mixer_nodes["mux"].params["select_index"] == 0
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
    assert tree_bytes(output) == tree_bytes(FULL_PROJECT, MANUAL_PROJECT_FILES)


def test_full_research_graph_is_published_but_evidence_project_is_hidden() -> None:
    examples = ProjectManager(ROOT).list_examples()
    assert "external_model_full" in examples
    assert "external_model_structural_reference" not in examples
