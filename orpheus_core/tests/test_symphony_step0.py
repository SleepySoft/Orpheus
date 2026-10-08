"""Symphony BAF 结构参考工程验证。"""

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
@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_symphony_baf_structural_reference_compile() -> None:
    """Baf1+Baf2 结构子组件能编译。"""
    plan = _compile("symphony_baf_structural_reference.yaml")
    comps = {cfg["component"] for cfg in plan.node_configs.values()}
    # 子组件展开后不应再出现 sub: 前缀
    assert not any(c.startswith("sub:") for c in comps)
    # 顶层反馈环被打断后，执行顺序中应出现延迟线节点
    assert any("feedback_delay" in nid for nid in plan.execution_order)
    # 主输出和 Audiopilot 输出节点都存在
    assert any("main_out" in nid for nid in plan.execution_order)
    assert any("ap_out" in nid for nid in plan.execution_order)
    assert sum(nid.startswith("model_1_2__") for nid in plan.node_configs) == 16
    assert plan.control_links == []


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_symphony_baf_structural_reference_run_end_to_end(client) -> None:
    """结构参考工程离线运行成功，主输出与 Audiopilot 输出均有能量。"""
    name = f"baf_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    src = _load_example("symphony_baf_structural_reference.yaml")
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = src["sample_rate"]
    doc["block_size"] = src["block_size"]
    doc["graph"] = src["graph"]
    doc["subcomponents"] = src.get("subcomponents", [])
    doc["control_connections"] = src.get("control_connections", [])
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200

    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    main = [p for p in result["probes"] if p["node"] == "main_probe" and p["param"] == "rms"]
    ap = [p for p in result["probes"] if p["node"] == "ap_probe" and p["param"] == "rms"]
    assert main and main[-1]["value"] > 0.01
    assert ap and ap[-1]["value"] > 0.01
