"""数据点 layout 测试：复杂嵌套模型（examples/dsp_model_reference.yaml）的
树形编目、数据类型分类、导出可读性与回写往返。"""

from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.parameter_catalog import apply_payload, build_catalog, export_payload
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
    assert "双二阶滤波器组" in text
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
