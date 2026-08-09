"""新实现组件（switch / saturation / window / matrix_mul / limiter /
soft_clipper / noise_slew / level_detect）的数值正确性验证：离线运行 +
输出 WAV 数值 / 探针检查。"""

from __future__ import annotations

import struct
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


def _run(client, nodes, conns, samples, ch: int = 1) -> tuple[list[float], list[dict]]:
    name = f"dyn_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    _write_input(pdir, samples, ch)
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = 48000
    doc["block_size"] = 128
    doc["graph"] = {"nodes": nodes, "connections": conns}
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    out = pdir / "outputs" / "out.wav"
    with wave.open(str(out), "rb") as w:
        data = w.readframes(w.getnframes())
    vals = [v / 32767.0 for v in struct.unpack(f"<{len(data) // 2}h", data)]
    return vals, result["probes"]


def _chain(nodes):
    return [
        {"from": f"{nodes[i]}:out", "to": f"{nodes[i + 1]}:in"}
        for i in range(len(nodes) - 1)
    ]


def _io_nodes(comp_id: str, params: dict, *, ch: int = 1, y: int = 0) -> list[dict]:
    return [
        {
            "id": "wav_in",
            "component": "orpheus.builtin.wav_in",
            "params": {"file_path": "input.wav", "channels": ch},
            "position": {"x": 0, "y": y},
        },
        {
            "id": comp_id,
            "component": f"orpheus.builtin.{comp_id}",
            "params": {**params, "channels": ch},
            "position": {"x": 220, "y": y},
        },
        {
            "id": "wav_out",
            "component": "orpheus.builtin.wav_out",
            "params": {"file_path": "outputs/out.wav", "channels": ch, "sample_rate": 48000},
            "position": {"x": 440, "y": y},
        },
    ]


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_saturation_hard_clip(client) -> None:
    """饱和限幅（hard）：|x|≤limit 直通，超限削到 ±limit。"""
    nodes = _io_nodes("saturation", {"limit": 0.5, "soft": 0.0}, ch=1)
    samples = [0.25, 1.0, -0.8, 0.0] * 64
    vals, _ = _run(client, nodes, _chain([n["id"] for n in nodes]), samples)
    assert abs(vals[0] - 0.25) < 2e-3
    assert abs(vals[1] - 0.5) < 2e-3
    assert abs(vals[2] + 0.5) < 2e-3
    assert abs(vals[3]) < 2e-3
    assert max(abs(v) for v in vals) <= 0.502


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_switch_enable_and_window(client) -> None:
    """开关：enable=0 输出静音；窗函数：按系数逐样本加权，超窗长直通。"""
    # switch 关断（ramp 0 → 立即静音）
    nodes = _io_nodes("switch", {"enable": 0.0, "ramp_ms": 0.0}, ch=1)
    vals, _ = _run(client, nodes, _chain([n["id"] for n in nodes]), [0.5] * 256)
    assert all(abs(v) < 2e-3 for v in vals)

    # 窗函数：coeffs=[0.5, 1, 0.5, 1]，窗长 4
    nodes = _io_nodes(
        "window",
        {"window_size": 4, "coefficients": "0.5, 1.0, 0.5, 1.0"},
        ch=1,
    )
    samples = [0.2, 0.4, 0.6, 0.8] + [0.5] * 128
    vals, _ = _run(client, nodes, _chain([n["id"] for n in nodes]), samples)
    assert abs(vals[0] - 0.1) < 3e-3
    assert abs(vals[1] - 0.4) < 3e-3
    assert abs(vals[2] - 0.3) < 3e-3
    assert abs(vals[3] - 0.8) < 3e-3
    assert abs(vals[4] - 0.5) < 3e-3  # 超窗长直通


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_matrix_mul_scales_channels(client) -> None:
    """矩阵乘法：M=2I（立体声）→ 每通道 ×2。"""
    nodes = _io_nodes(
        "matrix_mul",
        {"rows": 2, "cols": 2, "matrix": "2.0, 0.0, 0.0, 2.0"},
        ch=2,
    )
    samples = [0.3, 0.5] * 128
    vals, _ = _run(client, nodes, _chain([n["id"] for n in nodes]), samples, ch=2)
    assert abs(vals[0] - 0.6) < 5e-3
    assert abs(vals[1] - 1.0) < 5e-3


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_limiter_compresses_and_soft_clipper_saturates(client) -> None:
    """限幅器：阈值 -6dB、输入 1.0 → 稳态输出 ≈0.5；软削波：drive=6dB 输出有界。"""
    nodes = _io_nodes("limiter", {"threshold_db": -6.0, "attack_ms": 5.0, "release_ms": 100.0}, ch=1)
    vals, _ = _run(client, nodes, _chain([n["id"] for n in nodes]), [1.0] * 4800)
    assert max(vals) <= 1.0
    # 输出尾部可能被运行时零填充，取 90% 处近似稳态
    assert abs(vals[int(len(vals) * 0.9)] - 0.5) < 0.1  # 稳态增益 ≈ 阈值/包络

    nodes = _io_nodes("soft_clipper", {"drive_db": 6.0}, ch=1)
    vals, _ = _run(client, nodes, _chain([n["id"] for n in nodes]), [0.25] * 256)
    assert 0.4 < vals[0] < 0.6  # tanh(2*0.25)/tanh(2) ≈ 0.48
    assert all(abs(v) <= 1.0 for v in vals)


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_noise_slew_limits_step(client) -> None:
    """变化率限幅：0→1 阶跃被限制为逐样本爬升（0.1/s @48k ≈ 2.08e-6）。"""
    nodes = _io_nodes("noise_slew", {"rise_rate": 0.1, "fall_rate": 0.1}, ch=1)
    vals, _ = _run(client, nodes, _chain([n["id"] for n in nodes]), [0.0] * 128 + [1.0] * 128)
    max_delta = max(abs(vals[i] - vals[i - 1]) for i in range(1, len(vals)))
    assert max_delta <= 3.1e-5  # 16bit 量化步长 1/32767 ≈ 3.05e-5
    assert max(vals) < 0.01  # 128 样本爬升后仍远小于 1


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_level_detect_probe_and_passthrough(client) -> None:
    """电平检测：音频直通 + level 探针上报峰值包络。"""
    nodes = _io_nodes("level_detect", {"mode": 0, "attack_ms": 5.0, "release_ms": 100.0}, ch=1)
    vals, probes = _run(client, nodes, _chain([n["id"] for n in nodes]), [0.25] * 4800)
    assert abs(vals[0] - 0.25) < 3e-3  # 直通
    levels = [p for p in probes if p["node"] == "level_detect" and p["param"] == "level"]
    assert levels and max(p["value"] for p in levels) > 0.2
