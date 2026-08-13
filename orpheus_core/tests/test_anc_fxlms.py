"""主动降噪组件 FxLMS 验证：anc_fxlms。

原理简述：外部参考麦 x（噪声）与误差麦 d（耳道内残余噪声）
输入，自适应滤波器学习“期望抵消的反相信号”；
noise_reduction_db = 10*log10(P_d / P_e)，正值表示降噪。
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


_RT_BUILT = pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)


def _write_wav(pdir: Path, fname: str, samples: list[float], ch: int = 1) -> None:
    n = len(samples) // ch
    arr = np.asarray(samples, dtype=np.float32).reshape(n, ch)
    with wave.open(str(pdir / fname), "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes((np.clip(arr, -1, 1) * 32767).astype("<i2").tobytes())


def _sine(freq: float, n: int, amp: float = 0.5, sr: int = 48000) -> list[float]:
    return [amp * math.sin(2 * math.pi * freq * i / sr) for i in range(n)]


def _run_anc(client, params: dict, x: list[float], d: list[float], ch: int = 1) -> dict:
    name = f"anc_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    _write_wav(pdir, "x.wav", x, ch)
    _write_wav(pdir, "d.wav", d, ch)
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = 48000
    doc["block_size"] = 128
    doc["graph"] = {
        "nodes": [
            {"id": "wx", "component": "orpheus.builtin.wav_in", "params": {"file_path": "x.wav", "channels": ch}, "position": {"x": 0, "y": 0}},
            {"id": "wd", "component": "orpheus.builtin.wav_in", "params": {"file_path": "d.wav", "channels": ch}, "position": {"x": 0, "y": 80}},
            {"id": "anc", "component": "orpheus.builtin.anc_fxlms", "params": {**params, "channels": ch}, "position": {"x": 200, "y": 0}},
            {"id": "out", "component": "orpheus.builtin.wav_out", "params": {"file_path": "outputs/out.wav", "channels": ch, "sample_rate": 48000}, "position": {"x": 400, "y": 0}},
        ],
        "connections": [
            {"from": "wx:out", "to": "anc:x"},
            {"from": "wd:out", "to": "anc:d"},
            {"from": "anc:out", "to": "out:in"},
        ],
    }
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    return {p["param"]: p["value"] for p in result.get("probes", []) if p["node"] == "anc"}


@_RT_BUILT
def test_anc_fxlms_cancels_tone(client) -> None:
    """单一正弦，d = x（简化模型）→ 收敛后 noise_reduction_db 显著为正，residual异压。"""
    n = 48000
    x = _sine(200.0, n, amp=0.5)
    probes = _run_anc(client, {"filter_length": 256, "step_size": 0.05}, x, x)
    nr = probes.get("noise_reduction_db")
    pd = probes.get("power_d", 1.0)
    pe = probes.get("power_e", 1.0)
    assert nr is not None, "noise_reduction_db probe missing"
    assert nr > 10, f"expected clear noise reduction, got {nr} dB"
    assert pe < pd, "residual should be lower than the error-mic power"


@_RT_BUILT
def test_anc_fxlms_with_secondary_delay(client) -> None:
    """引入次路延迟后仍能收敛并降噪（说明 filtered-x 对齐了相位）。"""
    n = 48000
    x = _sine(200.0, n, amp=0.5)
    probes = _run_anc(client, {"filter_length": 512, "step_size": 0.02, "secondary_delay": 3}, x, x)
    nr = probes.get("noise_reduction_db")
    assert nr is not None and nr > 6, f"should cancel despite secondary delay, got {nr}"
