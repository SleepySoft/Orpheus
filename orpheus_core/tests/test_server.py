"""API-level tests for the Orpheus HTTP server."""

from __future__ import annotations

import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app
from orpheus_core.server.rt import parse_probe_line

ROOT = Path(__file__).resolve().parents[2]  # repository root


@pytest.fixture()
def client():
    with TestClient(create_app(ROOT)) as c:
        yield c


@pytest.fixture()
def project(client):
    """Create a uniquely-named project and clean it up afterwards."""
    name = f"test_{uuid.uuid4().hex[:8]}"
    resp = client.post("/api/projects", json={"name": name})
    assert resp.status_code == 201, resp.text
    yield name
    client.delete(f"/api/projects/{name}")


def test_health_and_components(client):
    assert client.get("/api/health").json()["status"] == "ok"
    comps = client.get("/api/components").json()
    assert len(comps) >= 14
    by_id = {c["id"]: c for c in comps}
    gain = by_id["orpheus.builtin.gain"]
    assert {p["id"] for p in gain["ports"]} == {"in", "out"}
    assert any(p["id"] == "gain_db" for p in gain["parameters"])


def test_parse_probe_lines_scalar_and_json(client):
    from orpheus_core.server.app import _parse_probe_lines

    stdout = "\n".join(
        [
            "Input: test_input.wav 100 frames @ 48000 Hz",
            "PROBE n1 rms 0.5",
            "PROBE n2 peak 0.25",
            "PROBE_JSON scope waveform [0.1,-0.2,0.3]",
            "LOG lifecycle line",
        ]
    )
    probes = _parse_probe_lines(stdout)
    by = {(p["node"], p["param"]): p["value"] for p in probes}
    assert by[("n1", "rms")] == 0.5
    assert by[("n2", "peak")] == 0.25
    assert by[("scope", "waveform")] == [0.1, -0.2, 0.3]


def test_parse_probe_line_structured():
    assert parse_probe_line("PROBE a b 0.5") == ("a", "b", 0.5)
    assert parse_probe_line("PROBE_JSON a w [1,2,3]") == ("a", "w", [1, 2, 3])
    assert parse_probe_line("PROBE_JSON a w [1.5,-0.5]") == ("a", "w", [1.5, -0.5])
    assert parse_probe_line("LOG hello world") is None


def test_components_have_chinese_name_and_category(client):
    comps = client.get("/api/components").json()
    for c in comps:
        assert c["name"] and c["name"] != c["id"], f"{c['id']} missing display name"
        assert c["category"] and c["category"] != "未分类", f"{c['id']} missing category"


def test_project_lifecycle(client, project):
    # listed
    names = [p["name"] for p in client.get("/api/projects").json()]
    assert project in names

    # read back and modify
    doc = client.get(f"/api/projects/{project}").json()
    assert doc["graph"]["nodes"] == []
    doc["metadata"]["description"] = "updated"
    resp = client.put(f"/api/projects/{project}", json=doc)
    assert resp.status_code == 200, resp.text
    assert client.get(f"/api/projects/{project}").json()["metadata"]["description"] == "updated"

    # invalid document rejected
    resp = client.put(f"/api/projects/{project}", json={"foo": 1})
    assert resp.status_code == 400

    # delete -> 404
    assert client.delete(f"/api/projects/{project}").status_code == 200
    assert client.get(f"/api/projects/{project}").status_code == 404


def test_duplicate_project_rejected(client, project):
    resp = client.post("/api/projects", json={"name": project})
    assert resp.status_code == 409


def test_import_example_and_compile(client):
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "wav_gain_biquad"})
        assert resp.status_code == 201, resp.text
        doc = resp.json()["document"]
        assert len(doc["graph"]["nodes"]) == 4

        # absolute example paths rewritten to project-relative paths
        params = {n["id"]: n["params"] for n in doc["graph"]["nodes"]}
        assert params["wav_in"]["file_path"] == "test_input.wav"
        assert params["wav_out"]["file_path"] == "outputs/test_output.wav"
        pdir = ROOT / "workspace" / name
        assert (pdir / "test_input.wav").exists()

        resp = client.post(f"/api/projects/{name}/compile")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["nodes"] == 4
        assert len(result["execution_order"]) == 4
        assert (pdir / "project.plan.json").exists()
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_run_example_end_to_end(client):
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "wav_gain_biquad"})
        assert resp.status_code == 201, resp.text

        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["mode"] == "offline"  # pure file graph -> offline host
        assert result["status"] == "ok", result["stderr"]
        assert "outputs/test_output.wav" in result["outputs"]

        out = ROOT / "workspace" / name / "outputs" / "test_output.wav"
        assert out.exists() and out.stat().st_size > 44

        # file endpoint serves the output wav
        resp = client.get(f"/api/projects/{name}/files/outputs/test_output.wav")
        assert resp.status_code == 200
        assert len(resp.content) == out.stat().st_size

        # path traversal blocked
        assert client.get(f"/api/projects/{name}/files/../../README.md").status_code in (400, 404)

        # zip download
        resp = client.get(f"/api/projects/{name}/download")
        assert resp.status_code == 200
        assert resp.content[:2] == b"PK"
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_probe_waveform_readback_offline(client):
    """Probe waveform component reports a 1024-sample JSON array via PROBE_JSON."""
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "probe_waveform_scope"})
        assert resp.status_code == 201, resp.text

        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]

        by = {(p["node"], p["param"]): p["value"] for p in result["probes"]}
        wave = by.get(("scope", "waveform"))
        assert isinstance(wave, list), f"expected waveform array, got {type(wave)}"
        assert len(wave) == 1024
        assert all(isinstance(x, float) for x in wave)
        assert max(abs(x) for x in wave) > 0.1  # 440Hz sine captured
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_fir_taps_probe_reported(client):
    """非 .probe 命名组件的 PROBE 槽（fir.taps）也应被上报（探针发现走注册表）。"""
    import shutil

    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        assert client.post("/api/projects", json={"name": name}).status_code == 201
        pdir = ROOT / "workspace" / name
        shutil.copy2(ROOT / "examples" / "test_input.wav", pdir / "test_input.wav")
        doc = client.get(f"/api/projects/{name}").json()
        doc["graph"] = {
            "nodes": [
                {"id": "wav_in", "component": "orpheus.builtin.wav_in",
                 "params": {"file_path": "test_input.wav", "channels": 2},
                 "position": {"x": 0, "y": 0}},
                {"id": "fir1", "component": "orpheus.builtin.fir",
                 "params": {"coefficients": "0.5,0.5", "channels": 2},
                 "position": {"x": 200, "y": 0}},
                {"id": "wav_out", "component": "orpheus.builtin.wav_out",
                 "params": {"file_path": "outputs/out.wav", "channels": 2,
                            "sample_rate": 48000},
                 "position": {"x": 400, "y": 0}},
            ],
            "connections": [
                {"from": "wav_in:out", "to": "fir1:in"},
                {"from": "fir1:out", "to": "wav_out:in"},
            ],
        }
        assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        by = {(p["node"], p["param"]): p["value"] for p in result["probes"]}
        assert ("fir1", "taps") in by, f"fir.taps 探针未上报: {sorted(by)}"
        assert float(by[("fir1", "taps")]) == 2.0
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_mp3_in_offline_run(client):
    """MP3 input decodes via miniaudio and drives an offline run to WAV."""
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name})
        assert resp.status_code == 201, resp.text

        with open(ROOT / "examples" / "test_input.mp3", "rb") as f:
            up = client.post(
                f"/api/projects/{name}/uploads",
                files={"file": ("test_input.mp3", f, "audio/mpeg")},
            )
        assert up.status_code == 201, up.text
        mp3_path = up.json()["path"]

        doc = {
            "version": "0.1.0",
            "metadata": {"name": name, "description": "mp3 e2e"},
            "sample_rate": 48000,
            "block_size": 128,
            "tasks": [
                {"id": "default", "name": "Default", "sample_rate": 48000, "block_size": 128, "priority": 0}
            ],
            "graph": {
                "nodes": [
                    {
                        "id": "mp3",
                        "component": "orpheus.builtin.mp3_in",
                        "task": "default",
                        "params": {"file_path": mp3_path, "channels": 2},
                        "position": {"x": 0, "y": 0},
                    },
                    {
                        "id": "wav_out",
                        "component": "orpheus.builtin.wav_out",
                        "task": "default",
                        "params": {"file_path": "outputs/test_output.wav", "channels": 2, "sample_rate": 48000},
                        "position": {"x": 200, "y": 0},
                    },
                ],
                "connections": [{"from": "mp3:out", "to": "wav_out:in"}],
            },
        }
        resp = client.put(f"/api/projects/{name}", json=doc)
        assert resp.status_code == 200, resp.text

        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["mode"] == "offline"
        assert result["status"] == "ok", result["stderr"]

        out = ROOT / "workspace" / name / "outputs" / "test_output.wav"
        assert out.exists() and out.stat().st_size > 44
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_mp3_in_chinese_filename(client):
    """MP3 input must open UTF-8 (Chinese) filenames on Windows via wide APIs."""
    import shutil

    name = f"test_{uuid.uuid4().hex[:8]}"
    fname = "测试 音乐.mp3"
    try:
        resp = client.post("/api/projects", json={"name": name})
        assert resp.status_code == 201, resp.text
        pdir = ROOT / "workspace" / name
        shutil.copy2(ROOT / "examples" / "test_input.mp3", pdir / fname)

        doc = {
            "version": "0.1.0",
            "metadata": {"name": name, "description": "chinese filename"},
            "sample_rate": 48000,
            "block_size": 128,
            "tasks": [
                {"id": "default", "name": "Default", "sample_rate": 48000, "block_size": 128, "priority": 0}
            ],
            "graph": {
                "nodes": [
                    {
                        "id": "mp3",
                        "component": "orpheus.builtin.mp3_in",
                        "task": "default",
                        "params": {"file_path": fname, "channels": 2},
                        "position": {"x": 0, "y": 0},
                    },
                    {
                        "id": "wav_out",
                        "component": "orpheus.builtin.wav_out",
                        "task": "default",
                        "params": {"file_path": "outputs/test_output.wav", "channels": 2, "sample_rate": 48000},
                        "position": {"x": 200, "y": 0},
                    },
                ],
                "connections": [{"from": "mp3:out", "to": "wav_out:in"}],
            },
        }
        client.put(f"/api/projects/{name}", json=doc)
        resp = client.post(f"/api/projects/{name}/run")
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        assert (pdir / "outputs" / "test_output.wav").exists()
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_output_fanout_reaches_all_downstream(client):
    """A source output connected to multiple nodes must feed every consumer."""
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name})
        assert resp.status_code == 201, resp.text
        doc = {
            "version": "0.1.0",
            "metadata": {"name": name, "description": "fanout"},
            "sample_rate": 48000,
            "block_size": 128,
            "tasks": [
                {"id": "default", "name": "Default", "sample_rate": 48000, "block_size": 128, "priority": 0}
            ],
            "graph": {
                "nodes": [
                    {
                        "id": "sig",
                        "component": "orpheus.builtin.signal_gen",
                        "task": "default",
                        "params": {"frequency": 440.0, "amplitude": 0.5, "channels": 1},
                        "position": {"x": 0, "y": 0},
                    },
                    {
                        "id": "rms1",
                        "component": "orpheus.builtin.probe_rms",
                        "task": "default",
                        "params": {"channels": 1},
                        "position": {"x": 200, "y": 0},
                    },
                    {
                        "id": "rms2",
                        "component": "orpheus.builtin.probe_rms",
                        "task": "default",
                        "params": {"channels": 1},
                        "position": {"x": 200, "y": 120},
                    },
                    {
                        "id": "w1",
                        "component": "orpheus.builtin.wav_out",
                        "task": "default",
                        "params": {"file_path": "outputs/o1.wav", "channels": 1, "sample_rate": 48000},
                        "position": {"x": 400, "y": 0},
                    },
                    {
                        "id": "w2",
                        "component": "orpheus.builtin.wav_out",
                        "task": "default",
                        "params": {"file_path": "outputs/o2.wav", "channels": 1, "sample_rate": 48000},
                        "position": {"x": 400, "y": 120},
                    },
                ],
                "connections": [
                    {"from": "sig:out", "to": "rms1:in"},
                    {"from": "sig:out", "to": "rms2:in"},
                    {"from": "rms1:out", "to": "w1:in"},
                    {"from": "rms2:out", "to": "w2:in"},
                ],
            },
        }
        resp = client.put(f"/api/projects/{name}", json=doc)
        assert resp.status_code == 200, resp.text

        resp = client.post(f"/api/projects/{name}/run")
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        by = {(p["node"], p["param"]): p["value"] for p in result["probes"]}
        r1 = by.get(("rms1", "rms"))
        r2 = by.get(("rms2", "rms"))
        assert r1 is not None and r2 is not None, result["probes"]
        assert r1 > 0.1, f"first branch silent: {r1}"
        assert abs(r1 - r2) < 1e-6, f"fan-out branches differ: {r1} vs {r2}"

        pdir = ROOT / "workspace" / name
        b1 = (pdir / "outputs" / "o1.wav").read_bytes()
        b2 = (pdir / "outputs" / "o2.wav").read_bytes()
        assert b1 == b2 and len(b1) > 44
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_fir_matches_numpy_convolution(client):
    """FIR coefficients [0.5,0.5] must equal a two-tap moving average."""
    import math
    import wave

    import numpy as np

    name = f"test_{uuid.uuid4().hex[:8]}"
    n = 4800
    try:
        resp = client.post("/api/projects", json={"name": name})
        assert resp.status_code == 201, resp.text
        pdir = ROOT / "workspace" / name

        sine = np.array([0.8 * math.sin(2 * math.pi * 440.0 * i / 48000.0) for i in range(n)])
        with wave.open(str(pdir / "input.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes((np.clip(sine, -1, 1) * 32767).astype("<i2").tobytes())

        doc = {
            "version": "0.1.0",
            "metadata": {"name": name, "description": "fir numeric"},
            "sample_rate": 48000,
            "block_size": 128,
            "tasks": [
                {"id": "default", "name": "Default", "sample_rate": 48000, "block_size": 128, "priority": 0}
            ],
            "graph": {
                "nodes": [
                    {
                        "id": "wav_in",
                        "component": "orpheus.builtin.wav_in",
                        "task": "default",
                        "params": {"file_path": "input.wav", "channels": 1},
                        "position": {"x": 0, "y": 0},
                    },
                    {
                        "id": "fir",
                        "component": "orpheus.builtin.fir",
                        "task": "default",
                        "params": {"coefficients": "1.0,-1.0", "channels": 1},
                        "position": {"x": 200, "y": 0},
                    },
                    {
                        "id": "wav_out",
                        "component": "orpheus.builtin.wav_out",
                        "task": "default",
                        "params": {"file_path": "outputs/out.wav", "channels": 1, "sample_rate": 48000},
                        "position": {"x": 400, "y": 0},
                    },
                ],
                "connections": [
                    {"from": "wav_in:out", "to": "fir:in"},
                    {"from": "fir:out", "to": "wav_out:in"},
                ],
            },
        }
        client.put(f"/api/projects/{name}", json=doc)
        resp = client.post(f"/api/projects/{name}/run")
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]

        with wave.open(str(pdir / "outputs" / "out.wav"), "rb") as w:
            raw = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
        got = raw.astype(np.float32) / 32767.0

        block = 128
        padded_n = ((n + block - 1) // block) * block
        expected = np.zeros(padded_n, dtype=np.float32)
        expected[:n] = np.convolve(sine, [1.0, -1.0], mode="full")[:n]
        diff = np.max(np.abs(got[:n] - expected[:n]))
        assert diff < 0.01, f"FIR output deviates from convolution: max diff {diff}"
        assert np.max(np.abs(got[:n] - sine)) > 0.05, "output identical to input; filter not applied"
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_sweep_spectrum_peak_at_end_frequency(client):
    """After a 100->5000Hz log sweep, the final FFT peak sits near 5000Hz."""
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name})
        assert resp.status_code == 201, resp.text
        doc = {
            "version": "0.1.0",
            "metadata": {"name": name, "description": "sweep spectrum"},
            "sample_rate": 48000,
            "block_size": 128,
            "tasks": [
                {"id": "default", "name": "Default", "sample_rate": 48000, "block_size": 128, "priority": 0}
            ],
            "graph": {
                "nodes": [
                    {
                        "id": "sweep",
                        "component": "orpheus.builtin.sweep_gen",
                        "task": "default",
                        "params": {
                            "start_freq": 100.0,
                            "end_freq": 5000.0,
                            "duration_s": 10.0,
                            "amplitude": 0.8,
                            "log_scale": True,
                            "channels": 1,
                        },
                        "position": {"x": 0, "y": 0},
                    },
                    {
                        "id": "spec",
                        "component": "orpheus.builtin.probe_spectrum",
                        "task": "default",
                        "params": {"channels": 1, "window_size": 1024},
                        "position": {"x": 200, "y": 0},
                    },
                    {
                        "id": "wav_out",
                        "component": "orpheus.builtin.wav_out",
                        "task": "default",
                        "params": {"file_path": "outputs/sweep.wav", "channels": 1, "sample_rate": 48000},
                        "position": {"x": 400, "y": 0},
                    },
                ],
                "connections": [
                    {"from": "sweep:out", "to": "spec:in"},
                    {"from": "spec:out", "to": "wav_out:in"},
                ],
            },
        }
        client.put(f"/api/projects/{name}", json=doc)
        resp = client.post(f"/api/projects/{name}/run")
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]

        probes = {f"{p['node']}:{p['param']}": p["value"] for p in result["probes"]}
        bins = probes.get("spec:spectrum")
        assert isinstance(bins, list) and len(bins) == 512, f"bad spectrum: {type(bins)}"
        peak_idx = max(range(len(bins)), key=lambda i: bins[i])
        peak_freq = peak_idx * 48000 / 1024
        assert bins[peak_idx] > 0.05, f"spectrum flat: peak {bins[peak_idx]}"
        assert 4000 <= peak_freq <= 6000, f"peak at {peak_freq}Hz, expected ~5000Hz"
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_sweep_record_probe_curve(client):
    """扫频记录组件采集 频率→幅度 曲线，以 JSON 探针上报（供绘图控件消费）。"""
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        assert client.post("/api/projects", json={"name": name}).status_code == 201
        doc = client.get(f"/api/projects/{name}").json()
        doc["graph"] = {
            "nodes": [
                {"id": "sweep", "component": "orpheus.builtin.sweep_gen",
                 "params": {"start_freq": "100.0", "end_freq": "5000.0",
                            "duration_s": "10.0", "amplitude": "0.8",
                            "log_scale": "true", "channels": 1},
                 "position": {"x": 0, "y": 0}},
                {"id": "rec", "component": "orpheus.builtin.sweep_record",
                 "params": {"start_freq": "100.0", "end_freq": "5000.0",
                            "duration_s": "10.0", "log_scale": "true",
                            "bins": 32, "channels": 1},
                 "position": {"x": 200, "y": 0}},
                {"id": "wav_out", "component": "orpheus.builtin.wav_out",
                 "params": {"file_path": "outputs/out.wav", "channels": 1,
                            "sample_rate": 48000},
                 "position": {"x": 400, "y": 0}},
            ],
            "connections": [
                {"from": "sweep:out", "to": "rec:in"},
                {"from": "rec:out", "to": "wav_out:in"},
            ],
        }
        assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        by = {(p["node"], p["param"]): p["value"] for p in result["probes"]}
        curve = by.get(("rec", "sweep"))
        assert isinstance(curve, dict), f"expected sweep curve dict, got {type(curve)}"
        freq = curve.get("freq")
        mag = curve.get("mag")
        assert isinstance(freq, list) and len(freq) == 32, f"freq len {len(freq) if isinstance(freq, list) else freq}"
        assert isinstance(mag, list) and len(mag) == 32, f"mag len {len(mag) if isinstance(mag, list) else mag}"
        assert all(freq[i] < freq[i + 1] for i in range(len(freq) - 1)), "freq 应单调递增"
        assert curve.get("done") is True
        assert abs(float(curve.get("progress", 0)) - 1.0) < 1e-3
        # 0.8 幅度正弦 → RMS ≈ 0.566；允许首尾分箱不完整
        assert 0.3 < max(mag) < 0.9, f"幅度异常: max={max(mag)}"
        assert min(mag) > 0.05, f"存在空箱: min={min(mag)}"
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_sweep_gen_offline_duration_respected(client):
    """纯时钟图（无 wav_in）：离线宿主按扫频 duration_s 运行，不截断成固定 10s/1s。"""
    import wave

    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        assert client.post("/api/projects", json={"name": name}).status_code == 201
        doc = client.get(f"/api/projects/{name}").json()
        doc["graph"] = {
            "nodes": [
                {"id": "sweep", "component": "orpheus.builtin.sweep_gen",
                 "params": {"start_freq": "100.0", "end_freq": "5000.0",
                            "duration_s": "3.0", "amplitude": "0.7",
                            "log_scale": "true", "channels": 1},
                 "position": {"x": 0, "y": 0}},
                {"id": "wav_out", "component": "orpheus.builtin.wav_out",
                 "params": {"file_path": "outputs/out.wav", "channels": 1,
                            "sample_rate": 48000},
                 "position": {"x": 200, "y": 0}},
            ],
            "connections": [{"from": "sweep:out", "to": "wav_out:in"}],
        }
        assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        pdir = ROOT / "workspace" / name
        with wave.open(str(pdir / "outputs" / "out.wav"), "rb") as w:
            # 3s @48k = 144000 帧；块处理允许尾部丢一个块
            assert abs(w.getnframes() - 144000) <= 128, f"frames={w.getnframes()}"
    finally:
        client.delete(f"/api/projects/{name}")
