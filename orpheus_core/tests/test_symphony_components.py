"""Symphony 蒸馏组件端到端验证。

覆盖 6 个蒸馏组件（gain_ramper / iir_bank / rfft / ifft / sleeping_beauty /
input_mixer_3d）：示例工程编译 + 离线运行 + rfft->ifft round-trip 链路数值合理。
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

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "symphony_components_test.yaml"
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


def test_symphony_components_compile() -> None:
    """6 个蒸馏组件进入编译执行计划。"""
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = ProjectLoader().load(EXAMPLE)
    plan = GraphCompiler(registry).compile(project)
    comps = {cfg["component"] for cfg in plan.node_configs.values()}
    for cid in (
        "orpheus.builtin.gain_ramper",
        "orpheus.builtin.iir_bank",
        "orpheus.builtin.rfft",
        "orpheus.builtin.ifft",
        "orpheus.builtin.sleeping_beauty",
        "orpheus.builtin.input_mixer_3d",
    ):
        assert cid in comps, f"missing {cid}"


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_symphony_components_run_end_to_end(client) -> None:
    """示例工程离线运行：输出 WAV、rms 探针上报（rfft->ifft 链路数值合理）。"""
    name = f"symphony_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = 48000
    doc["block_size"] = 32
    with open(EXAMPLE, encoding="utf-8") as f:
        src = yaml.safe_load(f)
    doc["graph"] = src["graph"]
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200

    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    out = pdir / "symphony_components_test_out.wav"
    assert out.exists() and out.stat().st_size > 0
    rms = [p for p in result["probes"] if p["node"] == "probe" and p["param"] == "rms"]
    assert rms and max(p["value"] for p in rms) > 0.01
