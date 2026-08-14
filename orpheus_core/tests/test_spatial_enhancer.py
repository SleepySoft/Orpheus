"""spatial_enhancer??????v2 ?????

?? v1 haas_width ?????
- ?????width=1, air=0, mono_mix=1????????v1 ??????????
- side ????? width ???
- air ?????????????
- ? side ?????t0 ?????t0 ??????????v1 ?????
????? -> ????? / ??????
"""

from __future__ import annotations

import math
import struct
import uuid
import wave
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]
SR = 48000
BLOCK = 64

_DLL = ROOT / "build" / "components" / "liborpheus_builtin_spatial_enhancer.dll"


def _write_wav(path: Path, l: list[float], r: list[float]) -> None:
    n = len(l)
    assert len(r) == n
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        raw = bytearray()
        for a, b in zip(l, r):
            li = max(-1.0, min(1.0, a))
            ri = max(-1.0, min(1.0, b))
            raw += struct.pack("<hh", int(round(li * 32767)), int(round(ri * 32767)))
        w.writeframes(bytes(raw))


def _read_wav(path: Path) -> tuple[list[float], list[float]]:
    with wave.open(str(path), "rb") as w:
        assert w.getnchannels() == 2
        n = w.getnframes()
        frames = struct.unpack(f"<{n * 2}h", w.readframes(n))
    ls = [frames[2 * i] / 32768.0 for i in range(n)]
    rs = [frames[2 * i + 1] / 32768.0 for i in range(n)]
    return ls, rs


def _run(client, name: str, params: dict, out_name="out.wav"):
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = SR
    doc["block_size"] = BLOCK
    doc["graph"] = {
        "nodes": [
            {"id": "wav_in", "component": "orpheus.builtin.wav_in",
             "params": {"file_path": "in.wav", "channels": 2},
             "position": {"x": 0, "y": 0}},
            {"id": "se", "component": "orpheus.builtin.spatial_enhancer",
             "params": {"channels": 2, **params},
             "position": {"x": 200, "y": 0}},
            {"id": "wav_out", "component": "orpheus.builtin.wav_out",
             "params": {"file_path": "outputs/" + out_name, "channels": 2,
                        "sample_rate": SR},
             "position": {"x": 400, "y": 0}},
        ],
        "connections": [
            {"from": "wav_in:out", "to": "se:in"},
            {"from": "se:out", "to": "wav_out:in"},
        ],
    }
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    rj = client.post(f"/api/projects/{name}/run").json()
    assert rj["status"] == "ok", rj.get("stderr")
    return _read_wav(ROOT / "workspace" / name / "outputs" / out_name)


def _side_energy(ls, rs):
    return sum(((a - b) * 0.5) ** 2 for a, b in zip(ls, rs))


def _setup(client, name: str, l, r):
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    pdir.mkdir(parents=True, exist_ok=True)
    (pdir / "outputs").mkdir(parents=True, exist_ok=True)
    _write_wav(pdir / "in.wav", l, r)
    return pdir


def _needs_dll():
    if _DLL.exists():
        return
    pytest.skip("spatial_enhancer not built")


def test_spatial_neutral_passthrough_bittight() -> None:
    """?????width=1, air=0, mono_mix=1????????"""
    _needs_dll()
    name = f"se_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            n = 2000
            ls = [0.4 * math.sin(2 * math.pi * 440 * i / SR) for i in range(n)]
            rs = [0.4 * math.cos(2 * math.pi * 330 * i / SR) for i in range(n)]
            _setup(client, name, ls, rs)
            lo, ro = _run(client, name, {"width": 1.0, "air": 0.0, "mono_mix": 1.0})
            # int16 ???? + mid/side ????? 3 ? LSB?????? v1 ???
            tol = 3.0 / 32768.0 + 1e-9
            dl = max(abs(a - b) for a, b in zip(lo, ls))
            dr = max(abs(a - b) for a, b in zip(ro, rs))
            assert dl <= tol and dr <= tol, f"passthrough drift L={dl} R={dr}"
        finally:
            client.delete(f"/api/projects/{name}")


def test_spatial_width_scales_side_energy() -> None:
    """???side ??? width ???mid ??????"""
    _needs_dll()
    name = f"se_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            n = 4096
            # ?? mid + ?? side??????????
            import random
            rng = random.Random(42)
            ls, rs, mid = [], [], 0.2
            for i in range(n):
                d = (rng.random() - 0.5) * 0.4
                ls.append(mid + d)
                rs.append(mid - d)
            _setup(client, name, ls, rs)
            base_e = _side_energy(ls, rs)
            e_w4 = 0.0
            lo, ro = _run(client, name, {"width": 4.0, "air": 0.0, "mono_mix": 1.0})
            e_w4 = _side_energy(lo, ro)
            assert e_w4 > 4.0 * base_e * 0.85, (base_e, e_w4)
            # mid ???????????
            m0 = sum(ls) / n
            m_out = sum((a + b) * 0.5 for a, b in zip(lo, ro)) / n
            assert abs(m_out - m0) < 0.01, (m0, m_out)
        finally:
            client.delete(f"/api/projects/{name}")


def _bin_amp(sig, freq):
    """?? bin DFT ???????????????????????"""
    n = len(sig)
    w = 2 * math.pi * freq / SR
    c = sum(v * math.cos(w * i) for i, v in enumerate(sig))
    s = sum(v * math.sin(w * i) for i, v in enumerate(sig))
    return 2.0 * math.hypot(c, s) / n


def test_spatial_air_boosts_high_freq() -> None:
    """air>0?side ??????????????????????"""
    _needs_dll()
    name = f"se_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            n = SR
            ls, rs = [], []
            for i in range(n):
                low = 0.3 * math.sin(2 * math.pi * 200 * i / SR)
                high = 0.3 * math.sin(2 * math.pi * 8000 * i / SR)
                d = low + high  # L ? R ?? -> ? side?mid=0?? DC ???
                ls.append(d)
                rs.append(-d)
            _setup(client, name, ls, rs)
            lo, ro = _run(client, name, {"width": 1.0, "air": 0.0, "mono_mix": 1.0,
                                         "air_fc": 3000.0})
            side0 = [(a - b) * 0.5 for a, b in zip(lo, ro)]
            lo2, ro2 = _run(client, name, {"width": 1.0, "air": 0.7, "mono_mix": 1.0,
                                           "air_fc": 3000.0}, out_name="out2.wav")
            side1 = [(a - b) * 0.5 for a, b in zip(lo2, ro2)]
            hi0, hi1 = _bin_amp(side0, 8000), _bin_amp(side1, 8000)
            lo0, lo1 = _bin_amp(side0, 200), _bin_amp(side1, 200)
            # ????????????????????? -> ?/??????
            # ???fc=3000,48k??200Hz ???0.015?8kHz ???0.54?
            # ?? DC/????????
            assert hi1 >= hi0 * 0.2, (hi0, hi1)   # ???????????
            assert lo1 < lo0 * 0.6, (lo0, lo1)    # ?????????
            assert (hi1 / lo1) > (hi0 / lo0) * 1.5, (hi0, lo0, hi1, lo1)
        finally:
            client.delete(f"/api/projects/{name}")


def test_spatial_side_pulse_no_ghost() -> None:
    """? side ???t0 ?????t0 ??????????v1 ????"""
    _needs_dll()
    name = f"se_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            n = 2048
            ls = [0.0] * n
            rs = [0.0] * n
            ls[10] = 0.5
            rs[10] = -0.5
            _setup(client, name, ls, rs)
            lo, ro = _run(client, name, {"width": 1.0, "air": 0.0, "mono_mix": 1.0})
            # t10 ???????side=0.5 -> L=0.5,R=-0.5?
            assert abs(lo[10] - 0.5) <= 1 / 32768.0
            assert abs(ro[10] + 0.5) <= 1 / 32768.0
            # ??????????? 0???????
            for idx in range(0, n):
                if idx == 10:
                    continue
                assert abs(lo[idx]) <= 1 / 32768.0, f"ghost at t={idx} L={lo[idx]}"
                assert abs(ro[idx]) <= 1 / 32768.0, f"ghost at t={idx} R={ro[idx]}"
        finally:
            client.delete(f"/api/projects/{name}")


def test_spatial_no_clip_blocks() -> None:
    """???/?????? max|out| < 1?"""
    _needs_dll()
    name = f"se_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            import random
            rng = random.Random(7)
            n = 4096
            ls = [0.6 * math.sin(2 * math.pi * 300 * i / SR) + (rng.random() - 0.5) * 0.2
                  for i in range(n)]
            rs = [0.6 * math.cos(2 * math.pi * 400 * i / SR) + (rng.random() - 0.5) * 0.2
                  for i in range(n)]
            _setup(client, name, ls, rs)
            lo, ro = _run(client, name, {"width": 2.0, "air": 0.5, "mono_mix": 1.0})
            mx = max(max(abs(a) for a in lo), max(abs(b) for b in ro))
            assert mx <= 1.0, mx
        finally:
            client.delete(f"/api/projects/{name}")
