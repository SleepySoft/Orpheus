"""噪声检测组件读取验证：单端 noise_detector / 双端 noise_detector_ab。

覆盖：
- 单端：干净正弦—>频谱平坦度很低；白噪—>平坦度接近1、噪声底提高。
- 双端 A/B：参考与被测信号完全相同—>THD+N 趋近 -inf；在一路引入无关噪声—>THD+N/噪声占比显著升高。
基于 server /run 的离线路径取 probes（与 test_estimation_components 相同方式）。
"""

from __future__ import annotations

import math
import random
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


def _white(n: int, seed: int = 1, amp: float = 0.3) -> list[float]:
    rnd = random.Random(seed)
    return [amp * (rnd.random() * 2 - 1) for _ in range(n)]


def _put_project(client, name: str, doc: dict) -> None:
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200


def _run(client, name: str) -> list[dict]:
    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    return result.get("probes", [])


def _run_single(client, params: dict, samples: list[float], ch: int = 1) -> list[dict]:
    """单端：wav_in -> noise_detector -> wav_out；返回 probes。"""
    name = f"nd_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    _write_wav(pdir, "input.wav", samples, ch)
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = 48000
    doc["block_size"] = 128
    doc["graph"] = {
        "nodes": [
            {"id": "wav_in", "component": "orpheus.builtin.wav_in", "params": {"file_path": "input.wav", "channels": ch}, "position": {"x": 0, "y": 0}},
            {"id": "nd", "component": "orpheus.builtin.noise_detector", "params": {**params, "channels": ch}, "position": {"x": 200, "y": 0}},
            {"id": "out", "component": "orpheus.builtin.wav_out", "params": {"file_path": "outputs/out.wav", "channels": ch, "sample_rate": 48000}, "position": {"x": 400, "y": 0}},
        ],
        "connections": [
            {"from": "wav_in:out", "to": "nd:in"},
            {"from": "nd:out", "to": "out:in"},
        ],
    }
    _put_project(client, name, doc)
    return _run(client, name)


def _run_ab(client, params: dict, ref_samples: list[float], in_samples: list[float], ch: int = 1) -> list[dict]:
    """双端：两个 wav_in 分别接 ref / in，直通输出到 wav_out。"""
    name = f"ndab_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    _write_wav(pdir, "ref.wav", ref_samples, ch)
    _write_wav(pdir, "in.wav", in_samples, ch)
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = 48000
    doc["block_size"] = 128
    doc["graph"] = {
        "nodes": [
            {"id": "wr", "component": "orpheus.builtin.wav_in", "params": {"file_path": "ref.wav", "channels": ch}, "position": {"x": 0, "y": 0}},
            {"id": "wi", "component": "orpheus.builtin.wav_in", "params": {"file_path": "in.wav", "channels": ch}, "position": {"x": 0, "y": 80}},
            {"id": "ab", "component": "orpheus.builtin.noise_detector_ab", "params": {**params, "channels": ch}, "position": {"x": 200, "y": 0}},
            {"id": "out", "component": "orpheus.builtin.wav_out", "params": {"file_path": "outputs/out.wav", "channels": ch, "sample_rate": 48000}, "position": {"x": 400, "y": 0}},
        ],
        "connections": [
            {"from": "wr:out", "to": "ab:ref"},
            {"from": "wi:out", "to": "ab:in"},
            {"from": "ab:out", "to": "out:in"},
        ],
    }
    _put_project(client, name, doc)
    return _run(client, name)


# ---------- 单端 ----------


@_RT_BUILT
def test_noise_detector_clean_sine_flatness_low(client) -> None:
    """干净 1kHz 正弦—>频谱平坦度很低（集中字）。"""
    probes = _run_single(client, {}, _sine(1000.0, 4800))
    flat = [p for p in probes if p["node"] == "nd" and p["param"] == "flatness"]
    assert flat, "flatness probe missing"
    assert flat[-1]["value"] < 0.2


@_RT_BUILT
def test_noise_detector_white_noise_flatness_high(client) -> None:
    """白噪—>频谱平坦度接近 1。"""
    probes = _run_single(client, {}, _white(4800))
    flat = [p for p in probes if p["node"] == "nd" and p["param"] == "flatness"]
    assert flat, "flatness probe missing"
    assert flat[-1]["value"] > 0.5


@_RT_BUILT
def test_noise_detector_clip_detected(client) -> None:
    """窂波（幅值 1.4）—>窂波占比 > 0。"""
    probes = _run_single(client, {"clip_level": 0.999}, _sine(200.0, 4800, amp=1.4))
    clip = [p for p in probes if p["node"] == "nd" and p["param"] == "clip_pct"]
    assert clip, "clip_pct probe missing"
    assert clip[-1]["value"] > 0.0


# ---------- 双端 ----------


@_RT_BUILT
def test_noise_detector_ab_identical_low_thd(client) -> None:
    """参考与被测完全相同—>THD+N 很低，噪声占比趋近 0。"""
    s = _sine(1000.0, 4800)
    probes = _run_ab(client, {}, s, s)
    thd = [p for p in probes if p["node"] == "ab" and p["param"] == "thd_n_db"]
    ratio = [p for p in probes if p["node"] == "ab" and p["param"] == "noise_ratio"]
    assert thd, "thd_n_db probe missing"
    assert ratio, "noise_ratio probe missing"
    assert thd[-1]["value"] < -40, f"identical should be very low THD, got {thd[-1]['value']}"
    assert ratio[-1]["value"] < 0.1


@_RT_BUILT
def test_noise_detector_ab_noisy_upstream_raises(client) -> None:
    """被测路注引无关白噪—>THD+N 与噪声占比显著升高。"""
    ref_s = _sine(1000.0, 4800)
    wn = _white(4800, seed=7)
    in_s = [0.5 * ref_s[i] + 0.3 * wn[i] for i in range(len(ref_s))]
    probes = _run_ab(client, {}, ref_s, in_s)
    thd = [p for p in probes if p["node"] == "ab" and p["param"] == "thd_n_db"]
    ratio = [p for p in probes if p["node"] == "ab" and p["param"] == "noise_ratio"]
    assert thd, "thd_n_db probe missing"
    assert ratio, "noise_ratio probe missing"
    assert thd[-1]["value"] > -20, f"noisy upstream should raise THD, got {thd[-1]['value']}"
    assert ratio[-1]["value"] > 0.1


def _run_nlms(client, params: dict, ref_samples: list[float], in_samples: list[float], ch: int = 1) -> list[dict]:
    """NLMS 双端：两个 wav_in 接 ref / in，直通输出到 wav_out。"""
    name = f"ndnl_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    _write_wav(pdir, "ref.wav", ref_samples, ch)
    _write_wav(pdir, "in.wav", in_samples, ch)
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = 48000
    doc["block_size"] = 128
    doc["graph"] = {
        "nodes": [
            {"id": "wr", "component": "orpheus.builtin.wav_in", "params": {"file_path": "ref.wav", "channels": ch}, "position": {"x": 0, "y": 0}},
            {"id": "wi", "component": "orpheus.builtin.wav_in", "params": {"file_path": "in.wav", "channels": ch}, "position": {"x": 0, "y": 80}},
            {"id": "nl", "component": "orpheus.builtin.noise_detector_nlms", "params": {**params, "channels": ch}, "position": {"x": 200, "y": 0}},
            {"id": "out", "component": "orpheus.builtin.wav_out", "params": {"file_path": "outputs/out.wav", "channels": ch, "sample_rate": 48000}, "position": {"x": 400, "y": 0}},
        ],
        "connections": [
            {"from": "wr:out", "to": "nl:ref"},
            {"from": "wi:out", "to": "nl:in"},
            {"from": "nl:out", "to": "out:in"},
        ],
    }
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    return result.get("probes", [])


# ---------- NLMS 残差双端 ----------


@_RT_BUILT
def test_noise_detector_nlms_identical_low_residue(client) -> None:
    """参考与被测完全相同—>残差预算很低，占比趋近 0。"""
    s = _sine(1000.0, 48000)
    probes = _run_nlms(client, {"filter_length": 64, "step_size": 0.1}, s, s)
    res = [p for p in probes if p["node"] == "nl" and p["param"] == "residue_db"]
    ratio = [p for p in probes if p["node"] == "nl" and p["param"] == "noise_ratio"]
    assert res, "residue_db probe missing"
    assert ratio, "noise_ratio probe missing"
    assert res[-1]["value"] < -40, f"identical should have very low residue, got {res[-1]['value']}"
    assert ratio[-1]["value"] < 0.1


@_RT_BUILT
def test_noise_detector_nlms_additive_noise_raises_residue(client) -> None:
    """被测路引入无关白噪—>残差能量与噪声占比显著升高。"""
    ref_s = _sine(1000.0, 48000)
    wn = _white(48000, seed=11)
    in_s = [0.5 * ref_s[i] + 0.3 * wn[i] for i in range(len(ref_s))]
    probes = _run_nlms(client, {"filter_length": 64, "step_size": 0.1}, ref_s, in_s)
    res = [p for p in probes if p["node"] == "nl" and p["param"] == "residue_db"]
    ratio = [p for p in probes if p["node"] == "nl" and p["param"] == "noise_ratio"]
    assert res, "residue_db probe missing"
    assert ratio, "noise_ratio probe missing"
    assert res[-1]["value"] > -15, f"additive noise should raise residue, got {res[-1]['value']}"
    assert ratio[-1]["value"] > 0.1


@_RT_BUILT
def test_noise_detector_nlms_nonlinear_distortion_raises(client) -> None:
    """一路引入非线性失真（幅值三次拟合）—>非线性残差被 NLMS 捕获。"""
    ref_s = _sine(1000.0, 48000, amp=0.8)
    in_s = [x - 0.4 * (x ** 3) for x in ref_s]
    probes = _run_nlms(client, {"filter_length": 256, "step_size": 0.05}, ref_s, in_s)
    res = [p for p in probes if p["node"] == "nl" and p["param"] == "residue_db"]
    assert res, "residue_db probe missing"
    assert res[-1]["value"] > -30, f"nonlinear distortion should raise residue, got {res[-1]['value']}"
