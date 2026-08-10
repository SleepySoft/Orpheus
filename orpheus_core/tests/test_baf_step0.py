"""BAF SAS step0 / PostProcess 端到端骨架验证。

- baf_postprocess.yaml：32ch -> 22ch 后处理链路，覆盖 iir_bank/limiter/soft_clipper/
  delay_line/gain_ramper/input_select/output_router。
- baf_sas_step0.yaml：完整 step0 子组件层级结构，含 Medusa 反馈环（用 1 块延迟打破）。
"""

from __future__ import annotations

import uuid
from pathlib import Path

import pytest
import yaml
from fastapi.testclient import TestClient

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]
_CREATED: list[str] = []


@pytest.fixture()
def client():
    with TestClient(create_app(ROOT)) as c:
        yield c


@pytest.fixture(autouse=True)
def _cleanup(client):
    yield
    for name in _CREATED:
        try:
            client.delete(f"/api/projects/{name}")
        except Exception:
            pass
    _CREATED.clear()


def _load_example(name: str) -> dict:
    with open(ROOT / "examples" / name, encoding="utf-8") as f:
        return yaml.safe_load(f)


def _compile(name: str) -> dict:
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = ProjectLoader().load(ROOT / "examples" / name)
    project = flatten_project(project)
    return GraphCompiler(registry).compile(project)


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_baf_postprocess_compile() -> None:
    """PostProcess 骨架能编译，并包含预期组件。"""
    plan = _compile("baf_postprocess.yaml")
    comps = {cfg["component"] for cfg in plan.node_configs.values()}
    for cid in (
        "orpheus.builtin.input_select",
        "orpheus.builtin.gain_ramper",
        "orpheus.builtin.limiter",
        "orpheus.builtin.iir_bank",
        "orpheus.builtin.soft_clipper",
        "orpheus.builtin.output_router",
        "orpheus.builtin.delay_line",
    ):
        assert cid in comps, f"missing {cid}"


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_baf_postprocess_run_end_to_end(client) -> None:
    """PostProcess 骨架离线运行并产生非零输出。"""
    name = f"bpp_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    src = _load_example("baf_postprocess.yaml")
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = src["sample_rate"]
    doc["block_size"] = src["block_size"]
    doc["graph"] = src["graph"]
    doc["subcomponents"] = src.get("subcomponents", [])
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200

    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    rms = [p for p in result["probes"] if p["node"] == "probe" and p["param"] == "rms"]
    assert rms and rms[-1]["value"] > 0.01


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_baf_sas_step0_compile() -> None:
    """step0 子组件层级结构能编译。"""
    plan = _compile("baf_sas_step0.yaml")
    comps = {cfg["component"] for cfg in plan.node_configs.values()}
    # 子组件展开后不应再出现 sub: 前缀
    assert not any(c.startswith("sub:") for c in comps)
    # 顶层反馈环被打断后，执行顺序中应出现延迟线节点
    assert any("feedback_delay" in nid for nid in plan.execution_order)
    # 主输出和 Audiopilot 输出节点都存在
    assert any("main_out" in nid for nid in plan.execution_order)
    assert any("ap_out" in nid for nid in plan.execution_order)


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_baf_sas_step0_run_end_to_end(client) -> None:
    """step0 骨架离线运行成功，主输出与 Audiopilot 输出均有能量。"""
    name = f"bs0_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    src = _load_example("baf_sas_step0.yaml")
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = src["sample_rate"]
    doc["block_size"] = src["block_size"]
    doc["graph"] = src["graph"]
    doc["subcomponents"] = src.get("subcomponents", [])
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200

    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    main = [p for p in result["probes"] if p["node"] == "main_probe" and p["param"] == "rms"]
    ap = [p for p in result["probes"] if p["node"] == "ap_probe" and p["param"] == "rms"]
    assert main and main[-1]["value"] > 0.01
    assert ap and ap[-1]["value"] > 0.01
