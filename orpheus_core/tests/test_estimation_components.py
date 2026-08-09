"""估计/统计组件 readback 验证：psd / coherence_matrix / interp_lut。

覆盖：功率谱峰值 bin、全同信号相干≈1、查表插值数值；音频均直通。
"""

from __future__ import annotations

import math
import uuid
import wave
from pathlib import Path

import numpy as np
import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app

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


def _write_input(pdir: Path, samples: list[float], ch: int = 1) -> None:
    n = len(samples) // ch
    arr = np.asarray(samples, dtype=np.float32).reshape(n, ch)
    with wave.open(str(pdir / "input.wav"), "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes((np.clip(arr, -1, 1) * 32767).astype("<i2").tobytes())


def _run(client, comp_id: str, params: dict, samples: list[float], ch: int = 1) -> list[dict]:
    name = f"est_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    _write_input(pdir, samples, ch)
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = 48000
    doc["block_size"] = 128
    doc["graph"] = {
        "nodes": [
            {"id": "wav_in", "component": "orpheus.builtin.wav_in", "params": {"file_path": "input.wav", "channels": ch}, "position": {"x": 0, "y": 0}},
            {"id": "est", "component": f"orpheus.builtin.{comp_id}", "params": {**params, "channels": ch}, "position": {"x": 220, "y": 0}},
            {"id": "out", "component": "orpheus.builtin.wav_out", "params": {"file_path": "outputs/out.wav", "channels": ch, "sample_rate": 48000}, "position": {"x": 440, "y": 0}},
        ],
        "connections": [
            {"from": "wav_in:out", "to": "est:in"},
            {"from": "est:out", "to": "out:in"},
        ],
    }
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    return result["probes"]


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_psd_spectrum_peak_bin(client) -> None:
    """1kHz 正弦 → 功率谱峰值落在 bin≈2.7（48k/128 块），通道 0 readback 为幅度数组。"""
    n = 4800
    sine = [0.5 * math.sin(2 * math.pi * 1000.0 * i / 48000.0) for i in range(n)]
    probes = _run(client, "psd", {"smoothing": 4}, sine)
    spec = [p for p in probes if p["node"] == "est" and p["param"] == "spectrum"]
    assert spec
    bins = spec[-1]["value"]
    assert isinstance(bins, list) and len(bins) == 64  # block 128 → half 64
    peak = max(range(len(bins)), key=lambda k: bins[k])
    assert bins[peak] > 0.2
    assert 1 <= peak <= 5  # 1000Hz/48000*128 ≈ 2.7


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_coherence_matrix_identical_signals(client) -> None:
    """两通道完全相同 → 相干矩阵对角≈1、非对角≈1。"""
    n = 4800
    sine = [0.5 * math.sin(2 * math.pi * 440.0 * i / 48000.0) for i in range(n)]
    stereo = [v for pair in zip(sine, sine) for v in pair]  # 两通道相同
    probes = _run(client, "coherence_matrix", {"smoothing": 4}, stereo, ch=2)
    coh = [p for p in probes if p["node"] == "est" and p["param"] == "coherence"]
    assert coh
    value = coh[-1]["value"]
    assert isinstance(value, dict) and value["n"] == 2
    m = value["matrix"]
    assert len(m) == 4
    assert abs(m[0] - 1.0) < 0.05  # 对角
    assert m[1] > 0.9  # 全同信号 → 相干≈1
    assert m[2] > 0.9


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_interp_lut_linear_interpolation(client) -> None:
    """x_axis 0/10/20, y_axis 0/5/10，x=5 → y≈2.5；history 曲线 readback。"""
    probes = _run(
        client,
        "interp_lut",
        {"x_axis": "0, 10, 20", "y_axis": "0, 5, 10", "x": 5.0},
        [0.1] * 4800,
    )
    y = [p for p in probes if p["node"] == "est" and p["param"] == "y"]
    hist = [p for p in probes if p["node"] == "est" and p["param"] == "history"]
    assert y and abs(y[-1]["value"] - 2.5) < 1e-3
    assert hist and isinstance(hist[-1]["value"], list) and len(hist[-1]["value"]) > 0
