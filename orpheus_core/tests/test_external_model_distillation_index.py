"""Evidence-first progress gates for the dual-runtime external model."""

from pathlib import Path
import re

import yaml


ROOT = Path(__file__).resolve().parents[2]
INDEX = (
    ROOT
    / "docs"
    / "HOW"
    / "reference"
    / "external-models"
    / "model-b-distillation-index.yaml"
)


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
