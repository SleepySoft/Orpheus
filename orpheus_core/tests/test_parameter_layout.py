"""数据点 layout 测试：复杂嵌套模型（examples/dsp_model_reference.yaml）的
树形编目、数据类型分类、导出可读性与回写往返。"""

from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.parameter_catalog import apply_payload, build_catalog, component_data_points, export_payload
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def registry() -> Registry:
    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    return reg


@pytest.fixture(scope="module")
def model(registry: Registry):
    return ProjectLoader().load(ROOT / "examples" / "dsp_model_reference.yaml")


def test_model_loads_and_flattens(model) -> None:
    ids = set(flatten_project(model).graph.nodes)
    # 三层嵌套：主图 → crossover → low_band / high_band；主图 → front → eq_bank
    assert "crossover__low_band__biquad" in ids
    assert "crossover__high_band__biquad" in ids
    assert "front__eq_bank__bq" in ids
    assert "post__fir" in ids
    assert "wav_in" in ids and "wav_out" in ids


def test_model_compiles(model, registry: Registry) -> None:
    plan = GraphCompiler(registry).compile(flatten_project(model))
    assert len(plan.nodes) == 17
    assert len(plan.buffers) == 17
    assert plan.execution_order[0] == "wav_in"


def test_catalog_hierarchy_and_kinds(model, registry: Registry) -> None:
    entries = {e.flat_id: e for e in build_catalog(model, registry)}
    assert "front__eq_bank__bq" in entries
    assert "crossover__low_band__biquad" in entries
    assert "post__fir" in entries

    bq = entries["front__eq_bank__bq"]
    assert [x["id"] for x in bq.path] == ["front", "eq_bank", "bq"]

    # FIR 系数 = bulk（工程参数）；biquad_bank 系数 = 运行期 Bulk 槽
    fir_bulk = [p for p in entries["post__fir"].params_of("bulk") if not p.get("runtime")]
    assert [p["id"] for p in fir_bulk] == ["coefficients"]
    bank_bulk = entries["front__eq_bank__bq"].params_of("bulk")
    assert {p["id"] for p in bank_bulk} == {"bq0.coefs", "bq1.coefs"}
    assert all(p.get("runtime") for p in bank_bulk)

    # 探针分类（readback 推断），不进 setting
    mon_probes = [p["id"] for p in entries["front__mon"].params_of("probe")]
    assert "rms" in mon_probes
    assert [p["id"] for p in entries["post__fir"].params_of("probe")] == ["taps"]
    assert "rms" not in [p["id"] for p in entries["front__mon"].params_of("setting")]


def test_export_readable_and_roundtrip(model, registry: Registry) -> None:
    payload = export_payload(model, registry)
    text = json.dumps(payload, ensure_ascii=False, indent=2)
    # 可读性：无 NaN/None，含中文组件名与面包屑路径
    assert "NaN" not in text and "null" not in text
    assert "双段二阶滤波器" in text
    assert payload["nodes"][0]["node"] == "wav_in"
    fir = next(n for n in payload["nodes"] if n["node"] == "post__fir")
    assert fir["bulk"]["coefficients"] == [0.5, 0.25, -0.1, 0.05, 0.02]
    assert fir["values"]["channels"] == 2
    bank = next(n for n in payload["nodes"] if n["node"] == "front__eq_bank__bq")
    assert bank["values"]["gain_db1"] == 1.5

    # 回写往返：导出值全部能按 flatId 写回叶子节点
    cloned = copy.deepcopy(model)
    applied = apply_payload(cloned, payload, registry)
    assert applied == len(payload["nodes"])
    cloned_entries = {e.flat_id: e for e in build_catalog(cloned, registry)}
    for n in payload["nodes"]:
        entry = cloned_entries[n["node"]]
        for key, value in n["values"].items():
            assert entry.node_params[key] == value


def test_private_bulk_initialization_has_one_runtime_identity(model, registry):
    project = copy.deepcopy(model)
    entries = {entry.flat_id: entry for entry in build_catalog(project, registry)}
    bank = entries["front__eq_bank__bq"]
    payload = export_payload(project, registry)
    source = next(entry for entry in payload["nodes"] if entry["node"] == bank.flat_id)
    source["bulk"]["bq0_coefs"] = [0.5, 0.1, 0, 0, 0]
    apply_payload(project, payload, registry)
    plan = GraphCompiler(registry).compile(flatten_project(project))
    points = [entry for entry in plan.id_map if entry["node"] == bank.flat_id]
    coefficient_points = [entry for entry in points if entry["key"].startswith("bq")]
    assert [entry["key"] for entry in coefficient_points] == ["bq0.coefs", "bq1.coefs"]
    assert all(entry["runtime"] and entry["count"] == 5 and entry["double_bank"]
               for entry in coefficient_points)
    assert plan.node_configs[bank.flat_id]["params"]["bq0_coefs"] == "0.5, 0.1, 0, 0, 0"
    exported = next(entry for entry in export_payload(project, registry)["nodes"]
                    if entry["node"] == bank.flat_id)
    assert exported["bulk"]["bq0_coefs"] == [0.5, 0.1, 0, 0, 0]
    assert "bq0.coefs" not in exported["bulk"]


def test_resource_projection_preserves_ordinary_fields_and_initialization_defaults():
    manifest = {
        "parameters": [
            {"id": "plain", "type": "float"},
            {"id": "coefs_init", "type": "string", "default": "1, 0",
             "bulk": {"slot_key": "coefs"}},
        ],
        "bulk_slots": [{"id": "plain", "type": "float", "count": 2},
                       {"id": "coefs", "type": "float", "count": 2}],
    }
    before = copy.deepcopy(manifest)
    points = component_data_points(manifest)
    assert [point["id"] for point in points] == ["plain", "plain", "coefs"]
    assert points[-1]["initialization"]["id"] == "coefs_init"
    assert points[-1]["initialization"]["default"] == "1, 0"
    assert points[-1]["runtime"] is True
    assert manifest == before


def test_private_bulk_snapshot_includes_manifest_initialization_default(model, registry, monkeypatch):
    parameter = next(
        parameter
        for parameter in registry.get("orpheus.builtin.biquad_bank").manifest["parameters"]
        if parameter["id"] == "bq0_coefs"
    )
    monkeypatch.setitem(parameter, "default", "0.25, 0, 0, 0, 0")
    bank = next(entry for entry in export_payload(model, registry)["nodes"]
                if entry["node"] == "front__eq_bank__bq")
    assert bank["bulk"]["bq0_coefs"] == [0.25, 0, 0, 0, 0]
