"""Tests for device enumeration, project file list/upload, and probe readback."""

from __future__ import annotations

import io
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture()
def client():
    with TestClient(create_app(ROOT)) as c:
        yield c


@pytest.fixture()
def project(client):
    name = f"test_{uuid.uuid4().hex[:8]}"
    resp = client.post("/api/projects", json={"name": name})
    assert resp.status_code == 201, resp.text
    yield name
    client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_rt_host.exe").exists(), reason="rt_host not built"
)
def test_list_devices(client):
    resp = client.get("/api/devices")
    assert resp.status_code == 200, resp.text
    data = resp.json()
    assert "playback" in data and "capture" in data
    assert all("name" in d and "default" in d for d in data["playback"])


def test_upload_and_list_files(client, project):
    payload = b"RIFF" + b"\x00" * 100  # fake wav content is fine for storage test
    resp = client.post(
        f"/api/projects/{project}/uploads",
        files={"file": ("tone.wav", io.BytesIO(payload), "audio/wav")},
    )
    assert resp.status_code == 201, resp.text
    assert resp.json()["path"] == "tone.wav"

    # listed with extension filter
    files = client.get(f"/api/projects/{project}/files", params={"ext": ".wav"}).json()
    assert [f["path"] for f in files] == ["tone.wav"]

    # project.yaml not matched by wav filter, file content served back
    assert client.get(f"/api/projects/{project}/files").json() != []
    resp = client.get(f"/api/projects/{project}/files/tone.wav")
    assert resp.status_code == 200 and resp.content == payload


def test_upload_sanitizes_filename(client, project):
    resp = client.post(
        f"/api/projects/{project}/uploads",
        files={"file": ("../../evil.wav", io.BytesIO(b"x"), "audio/wav")},
    )
    assert resp.status_code == 201
    assert resp.json()["path"] == "evil.wav"
    assert (ROOT / "workspace" / project / "evil.wav").exists()


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_rt_host.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="rt_host and components not built",
)
def test_rt_session_lifecycle(client):
    """Start a realtime session, push a parameter, read logs, stop."""
    import time

    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "device_gain_biquad"})
        assert resp.status_code == 201, resp.text

        # /run auto-dispatches device graphs to a realtime session
        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        assert resp.json()["mode"] == "realtime"

        # wait for the host to come up (or fail on machines without audio devices)
        snap = None
        for _ in range(30):
            snap = client.get(f"/api/projects/{name}/rt/status").json()
            joined = "\n".join(snap["logs"])
            if "LOG rt_host running" in joined or not snap["running"]:
                break
            time.sleep(0.2)
        joined = "\n".join(snap["logs"])
        if "LOG rt_host running" not in joined:
            pytest.skip(f"audio device unavailable on this machine: {joined}")
        assert snap["running"]

        # live parameter push
        resp = client.post(
            f"/api/projects/{name}/rt/param",
            json={"node": "gain", "param": "gain_db", "value": -3.0},
        )
        assert resp.status_code == 200, resp.text
        time.sleep(0.5)
        snap = client.get(f"/api/projects/{name}/rt/status").json()
        assert any("OK SET gain gain_db" in line for line in snap["logs"])

        # duplicate start rejected
        assert client.post(f"/api/projects/{name}/rt/start").status_code == 409

        resp = client.post(f"/api/projects/{name}/rt/stop")
        assert resp.status_code == 200
        snap = client.get(f"/api/projects/{name}/rt/status").json()
        assert not snap["running"]
    finally:
        client.post(f"/api/projects/{name}/rt/stop")
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_generated_run_matches_dynamic_run(client):
    """Design principle: codegen mode and dynamic mode must produce identical audio."""
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "wav_gain_biquad"})
        assert resp.status_code == 201, resp.text
        out = ROOT / "workspace" / name / "outputs" / "test_output.wav"

        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200 and resp.json()["status"] == "ok", resp.text
        dynamic_bytes = out.read_bytes()

        resp = client.post(f"/api/projects/{name}/run_generated")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["mode"] == "generated"
        assert result["status"] == "ok", result["stderr"]
        generated_bytes = out.read_bytes()

        assert len(generated_bytes) == len(dynamic_bytes)
        assert generated_bytes == dynamic_bytes, "generated output differs from dynamic run"
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_generated_run_matches_dynamic_run_with_control_link(client):
    """含控制链路的工程：生成路径（control_tick 注入）与动态路径输出逐字节一致，且控制确实生效。

    control_link_demo：sig → gain（初始 -96dB）→ level_detect → wav_out，
    控制链 lvl:level → g:gain_db 闭环。无链基线增益保持 -96dB（近静默），输出必须不同。
    """
    name = f"test_{uuid.uuid4().hex[:8]}"
    base = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "control_link_demo"})
        assert resp.status_code == 201, resp.text
        out = ROOT / "workspace" / name / "outputs" / "test_output.wav"

        # 动态路径
        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200 and resp.json()["status"] == "ok", resp.text
        dynamic_bytes = out.read_bytes()

        # 生成路径（静态编译工程，含两相快照 control_tick）
        resp = client.post(f"/api/projects/{name}/run_generated")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["mode"] == "generated"
        assert result["status"] == "ok", result["stderr"]
        generated_bytes = out.read_bytes()

        assert len(generated_bytes) == len(dynamic_bytes)
        assert generated_bytes == dynamic_bytes, "含控制链的生成路径与动态路径输出不一致"

        # 无链基线：同图去掉 control_connections，增益保持 -96dB → 输出必须不同
        resp = client.post("/api/projects", json={"name": base, "from_example": "control_link_demo"})
        assert resp.status_code == 201, resp.text
        doc = client.get(f"/api/projects/{base}").json()  # GET 直接返回文档本体
        doc.pop("control_connections", None)
        resp = client.put(f"/api/projects/{base}", json=doc)
        assert resp.status_code == 200, resp.text
        base_out = ROOT / "workspace" / base / "outputs" / "test_output.wav"
        resp = client.post(f"/api/projects/{base}/run")
        assert resp.status_code == 200 and resp.json()["status"] == "ok", resp.text
        assert base_out.read_bytes() != dynamic_bytes, "控制链未生效（与无链基线输出相同）"
    finally:
        client.delete(f"/api/projects/{name}")
        client.delete(f"/api/projects/{base}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_run_returns_probe_readback(client):
    """signal_gen(0.5 sine) -> probe_rms -> wav_out: run response carries RMS value."""
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "signal_probe_wav"})
        assert resp.status_code == 201, resp.text
        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]

        probes = result["probes"]
        rms_entries = [p for p in probes if p["node"] == "rms" and p["param"] == "rms"]
        assert len(rms_entries) == 1, f"missing probe readback: {probes}"
        # 0.5 amplitude sine -> RMS ~ 0.354
        assert 0.3 < rms_entries[0]["value"] < 0.4
    finally:
        client.delete(f"/api/projects/{name}")
