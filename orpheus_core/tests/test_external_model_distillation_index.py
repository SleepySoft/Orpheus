"""Evidence-first progress gates for the dual-runtime external model."""

from pathlib import Path
import json
import os
import re
import subprocess
import sys

import yaml


ROOT = Path(__file__).resolve().parents[2]
INDEX = (
    ROOT
    / "examples"
    / "recycled"
    / "external_model_structural_reference"
    / "distillation-progress.yaml"
)
PROJECT_DIR = INDEX.parent
BIG6_EVIDENCE = PROJECT_DIR / "evidence" / "big6-defaults.json"
INPUT_ROUTING_EVIDENCE = PROJECT_DIR / "evidence" / "input-routing-defaults.json"
PREAMP_ROUTING_EVIDENCE = PROJECT_DIR / "evidence" / "preamp-routing-defaults.json"
POSTPROCESS_EVIDENCE = PROJECT_DIR / "evidence" / "postprocess-defaults.json"
_EXTERNAL_SOURCE_VALUE = os.environ.get("ORPHEUS_EXTERNAL_MODEL_A_ROOT")
EXTERNAL_SOURCE = Path(_EXTERNAL_SOURCE_VALUE) if _EXTERNAL_SOURCE_VALUE else None


def load_index() -> dict:
    return yaml.safe_load(INDEX.read_text(encoding="utf-8"))


def test_distillation_index_is_closed_and_graph_gates_are_honest() -> None:
    document = load_index()
    modules = document["modules"]
    module_ids = {module["id"] for module in modules}
    assert len(module_ids) == len(modules) == 25

    required = set(document["system_graph_gate"]["required_verified"])
    progress_values = set(document["progress_values"])
    artifact_values = set(document["artifact_values"])
    source_ids = set(document["sources"])
    for module in modules:
        assert module["purpose"]
        assert set(module["dependencies"]) <= module_ids
        assert set(module["evidence"]) <= source_ids
        assert set(module["progress"]) == required
        assert set(module["progress"].values()) <= progress_values
        assert module["artifact"] in artifact_values
        for artifact in module.get("evidence_artifacts", []):
            artifact_path = Path(artifact)
            assert not artifact_path.is_absolute()
            assert (PROJECT_DIR / artifact_path).is_file()
        eligible = all(module["progress"][gate] == "verified" for gate in required)
        assert module["system_graph_eligible"] is eligible


def test_cross_runtime_bus_field_counts_match_physical_contract() -> None:
    document = load_index()
    buses = {bus["id"]: bus for bus in document["cross_runtime_buses"]}
    assert set(buses) == {"bus_a_to_b", "bus_b_to_a"}
    assert buses["bus_a_to_b"]["total_values"] == 488
    assert buses["bus_b_to_a"]["total_values"] == 1193
    for bus in buses.values():
        assert sum(field["count"] for field in bus["fields"]) == bus["total_values"]


def test_pinned_source_fingerprints_are_portable_and_well_formed() -> None:
    sources = load_index()["sources"]
    for source in sources.values():
        assert not Path(source["file"]).is_absolute()
        fingerprint = source["sha256"]
        assert fingerprint == "per_file_pending" or re.fullmatch(r"[0-9a-f]{64}", fingerprint)
    assert sources["runtime_a_preamp_top"]["sha256"] != "per_file_pending"


def test_existing_heq_graph_is_still_blocked_from_full_system_claim() -> None:
    modules = {module["id"]: module for module in load_index()["modules"]}
    for module_id in ("B.HEADREST_HEQ_FIR", "B.OVERHEAD_HEQ_FIR"):
        module = modules[module_id]
        assert module["artifact"] == "slice_graph"
        assert module["system_graph_eligible"] is False
        assert module["progress"]["golden"] != "verified"
    for module_id in ("B.HEADREST_HEQ_IIR", "B.OVERHEAD_HEQ_IIR"):
        assert modules[module_id]["progress"]["state"] != "verified"


def test_big6_evidence_records_real_layouts_without_claiming_golden() -> None:
    evidence = json.loads(BIG6_EVIDENCE.read_text(encoding="utf-8"))
    facts = evidence["facts"]
    assert facts["processing_order"] == ["volume", "bass", "treble", "mid", "balance"]
    assert facts["volume"]["channel_map"]["values"] == [10, *range(1, 11)]
    assert facts["balance"]["channel_to_ramper"]["values"] == [1, 2, 3, 4, 1, 2, 1, 1, 2, 2]
    assert facts["balance"]["offset"]["value"] == 128.0
    assert len(facts["balance"]["table_db"]["values"]) == 30
    assert len(facts["bass"]["boost_coefficients"]["values"]) == 3
    assert len(facts["mid"]["boost_coefficients"]["values"]) == 5
    assert len(facts["treble"]["boost_coefficients"]["values"]) == 3
    modules = {module["id"]: module for module in load_index()["modules"]}
    for module_id in evidence["module_ids"]:
        assert modules[module_id]["progress"]["parameters"] == "extracted"
        assert modules[module_id]["progress"]["golden"] == "pending"


def test_big6_evidence_is_reproducible_when_external_source_is_available(tmp_path: Path) -> None:
    if EXTERNAL_SOURCE is None or not EXTERNAL_SOURCE.is_dir():
        return
    output = tmp_path / "big6-defaults.json"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "extract_external_big6_index.py"),
            str(EXTERNAL_SOURCE),
            str(output),
        ],
        check=True,
        cwd=ROOT,
    )
    assert output.read_bytes() == BIG6_EVIDENCE.read_bytes()


def test_input_routing_evidence_preserves_shared_route_then_select_semantics() -> None:
    evidence = json.loads(INPUT_ROUTING_EVIDENCE.read_text(encoding="utf-8"))
    select = evidence["facts"]["input_select"]
    mixer = evidence["facts"]["input_mixer"]
    assert select["default_route"]["values"] == list(range(1, 31))
    assert select["entertainment"]["values"] == list(range(1, 13))
    assert select["announcement"]["values"] == list(range(13, 30))
    assert select["microphone"]["value"] == 30
    assert "route first" in select["selection_order"]
    assert mixer["input_order"] == [
        "L", "R", "C", "LFE", "Lss", "Rss", "Lsr", "Rsr", "Ltf", "Ltb", "Rtf", "Rtb"
    ]
    assert mixer["stereo_downmix_default"]["value"] is False
    assert mixer["weights_514"]["values"] == [3.16, 1.0, 1.0]
    modules = {module["id"]: module for module in load_index()["modules"]}
    for module_id in evidence["module_ids"]:
        assert modules[module_id]["progress"]["parameters"] == "extracted"
        assert modules[module_id]["progress"]["golden"] == "pending"


def test_input_routing_evidence_is_reproducible_when_external_source_is_available(
    tmp_path: Path,
) -> None:
    if EXTERNAL_SOURCE is None or not EXTERNAL_SOURCE.is_dir():
        return
    output = tmp_path / "input-routing-defaults.json"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "extract_external_input_index.py"),
            str(EXTERNAL_SOURCE),
            str(output),
        ],
        check=True,
        cwd=ROOT,
    )
    assert output.read_bytes() == INPUT_ROUTING_EVIDENCE.read_bytes()


def test_preamp_routing_evidence_keeps_main_and_adaptive_branches_distinct() -> None:
    evidence = json.loads(PREAMP_ROUTING_EVIDENCE.read_text(encoding="utf-8"))
    facts = evidence["facts"]
    branches = facts["branch_provenance"]
    assert branches["post_buffer_32"]["source"] == "volume_audio_10"
    assert branches["adaptive_buffer_10"]["source"] == "balanced_audio_10"
    assert branches["microphone_buffer_1"]["source"] == "input_select.microphone_1"
    assert branches["level_detection_audio_10"]["source"] == "input_mixer.program_10"
    route = facts["post_buffer_route"]
    assert route["default_map"]["values"] == list(range(1, 7)) * 5 + [1, 2]
    assert route["valid_source_channels"] == list(range(1, 7))
    detector = facts["level_detection"]
    assert detector["delay_samples"]["value"] == 72
    assert detector["pre_emphasis_stages"]["values"] == [1] * 10
    assert len(detector["pre_emphasis_coefficients"]["values"]) == 50
    module = {item["id"]: item for item in load_index()["modules"]}["A.PREAMP_ROUTE_LEVEL"]
    assert module["progress"]["parameters"] == "extracted"
    assert module["progress"]["golden"] == "pending"


def test_preamp_routing_evidence_is_reproducible_when_external_source_is_available(
    tmp_path: Path,
) -> None:
    if EXTERNAL_SOURCE is None or not EXTERNAL_SOURCE.is_dir():
        return
    output = tmp_path / "preamp-routing-defaults.json"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "extract_external_preamp_route_index.py"),
            str(EXTERNAL_SOURCE),
            str(output),
        ],
        check=True,
        cwd=ROOT,
    )
    assert output.read_bytes() == PREAMP_ROUTING_EVIDENCE.read_bytes()


def test_postprocess_evidence_preserves_two_layer_stage_order_and_layouts() -> None:
    evidence = json.loads(POSTPROCESS_EVIDENCE.read_text(encoding="utf-8"))
    facts = evidence["facts"]
    assert facts["stage_order"] == [
        "internal_gain_ramp",
        "limiter",
        "post_eq",
        "internal_soft_clip",
        "rmdl_mute",
        "asd_main_aux_router",
        "all_mute",
        "output_soft_clip",
        "output_calibration",
        "freq_comp",
        "output_delay",
    ]
    assert facts["routing"]["main_select"]["values"] == list(range(1, 23))
    assert facts["limiter"]["attack"]["high"]["count"] == 22
    assert facts["post_eq"]["coefficients"]["count"] == 560
    assert facts["freq_comp"]["coefficients"]["count"] == 175
    assert facts["output_delay"]["samples"]["values"] == [0] * 22
    assert facts["soft_clip_internal"]["high"]["xmin"]["value"] == 0.65
    assert facts["soft_clip_output"]["high"]["xmin"]["value"] == 2.0
    assert facts["output_calibration"]["default_values"] == "pending"
    module = {item["id"]: item for item in load_index()["modules"]}["A.POST_PROCESS"]
    assert module["progress"]["parameters"] == "extracted"
    assert module["progress"]["state"] != "verified"
    assert module["progress"]["golden"] == "pending"


def test_postprocess_evidence_is_reproducible_when_external_source_is_available(
    tmp_path: Path,
) -> None:
    if EXTERNAL_SOURCE is None or not EXTERNAL_SOURCE.is_dir():
        return
    output = tmp_path / "postprocess-defaults.json"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "extract_external_postprocess_index.py"),
            str(EXTERNAL_SOURCE),
            str(output),
        ],
        check=True,
        cwd=ROOT,
    )
    assert output.read_bytes() == POSTPROCESS_EVIDENCE.read_bytes()
