"""蒸馏模型一键导入端点测试：YAML → 新工程；model_tree/presets 等顶层字段往返保留；可编译。"""

from __future__ import annotations

import uuid
import shutil
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]


def test_distill_import_roundtrip_and_extra_fields() -> None:
    name = f"distill_{uuid.uuid4().hex[:8]}"
    text = (ROOT / "examples" / "dsp_model_reference.yaml").read_text(encoding="utf-8")
    with TestClient(create_app(ROOT)) as client:
        resp = client.post(f"/api/projects/{name}/distill", json={"yaml": text})
        assert resp.status_code == 200, resp.text
        doc = resp.json()["document"]
        # model_tree 注释随导入保留（顶层未知字段）
        assert doc["model_tree"]["name"].startswith("参考")

        # presets 等未知顶层字段经 put → 重载 → get 往返不丢
        doc["presets"] = [{"name": "p1", "created_at": "", "nodes": []}]
        assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
        got = client.get(f"/api/projects/{name}").json()
        assert got["presets"][0]["name"] == "p1"
        assert got["model_tree"]["name"].startswith("参考")

        # 三层嵌套编译通过
        compiled = client.post(f"/api/projects/{name}/compile")
        assert compiled.status_code == 200, compiled.text
        assert len(compiled.json()["execution_order"]) == 17


def test_distill_import_rejects_invalid_yaml() -> None:
    name = f"distill_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        resp = client.post(f"/api/projects/{name}/distill", json={"yaml": "{{{{not yaml"})
        assert resp.status_code == 400
        resp = client.post(f"/api/projects/{name}/distill", json={"yaml": "foo: bar"})
        assert resp.status_code == 400
        assert "缺少 graph" in resp.json()["detail"]


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_distilled_model_runs_end_to_end() -> None:
    """蒸馏导入的复杂嵌套模型离线运行：输出 WAV，且嵌套层探针以 flatId 上报。"""
    name = f"distill_{uuid.uuid4().hex[:8]}"
    text = (ROOT / "examples" / "dsp_model_reference.yaml").read_text(encoding="utf-8")
    with TestClient(create_app(ROOT)) as client:
        assert client.post(f"/api/projects/{name}/distill", json={"yaml": text}).status_code == 200
        pdir = ROOT / "workspace" / name
        shutil.copy2(ROOT / "examples" / "test_input.wav", pdir / "test_input.wav")
        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        by = {(p["node"], p["param"]): p["value"] for p in result["probes"]}
        # 三层嵌套内的探针：front__mon.rms、post__fir.taps、out_mon.rms
        assert ("front__mon", "rms") in by
        assert ("post__fir", "taps") in by
        assert ("out_mon", "rms") in by
        assert (pdir / "outputs" / "model_out.wav").exists()
