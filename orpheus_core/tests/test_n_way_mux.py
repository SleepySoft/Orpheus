"""n_way_mux 数值正确性测试（select 为 1 起整数：1=in0、2=in1 …）。
覆盖：按 select 选路；可变引脚 in0..inN-1 绑定（未接引脚静音）；select 越界钳位。"""
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
_DLL = ROOT / "build" / "components" / "liborpheus_builtin_n_way_mux.dll"
_SKIP = not _DLL.exists()


def _write_wav(path, frames):
    n = len(frames[0])
    with wave.open(str(path), "wb") as w:
        w.setnchannels(len(frames))
        w.setsampwidth(2)
        w.setframerate(SR)
        raw = bytearray()
        for i in range(n):
            for ch_vals in frames:
                v = max(-1.0, min(1.0, ch_vals[i]))
                raw += struct.pack("<h", int(round(v * 32767)))
        w.writeframes(bytes(raw))


def _read_wav(path):
    with wave.open(str(path), "rb") as w:
        chans = w.getnchannels()
        n = w.getnframes()
        frames = struct.unpack(f"<{n*chans}h", w.readframes(n))
    # interleaved [L0,R0,L1,R1,...]: channel ch at frame i = frames[i*chans+ch]
    return [[frames[i * chans + ch] / 32768.0 for i in range(n)] for ch in range(chans)]


def _mux_run(client, name, src_files, select, inputs, ramp_ms=20.0, out_name="out.wav"):
    nodes = [{"id": "wav_out", "component": "orpheus.builtin.wav_out",
              "params": {"file_path": "outputs/" + out_name, "channels": 2,
                         "sample_rate": SR}, "position": {"x": 800, "y": 0}}]
    conns = []
    for idx, path in src_files.items():
        nodes.append({"id": f"src{idx}", "component": "orpheus.builtin.wav_in",
                      "params": {"file_path": path, "channels": 2},
                      "position": {"x": 0, "y": idx * 120}})
        conns.append({"from": f"src{idx}:out", "to": f"mux:in{idx}"})
    nodes.append({"id": "mux", "component": "orpheus.builtin.n_way_mux",
                  "params": {"inputs": inputs, "channels": 2, "select": select,
                             "ramp_ms": ramp_ms},
                  "position": {"x": 300, "y": 0}})
    conns.append({"from": "mux:out", "to": "wav_out:in"})

    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = SR
    doc["block_size"] = BLOCK
    doc["graph"] = {"nodes": nodes, "connections": conns}
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    rj = client.post(f"/api/projects/{name}/run").json()
    assert rj["status"] == "ok", rj.get("stderr")
    out = _read_wav(ROOT / "workspace" / name / "outputs" / out_name)
    return out, rj


@pytest.mark.skipif(_SKIP, reason="n_way_mux not built")
def test_mux_selects_input():
    name = f"mux_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            pdir = ROOT / "workspace" / name
            pdir.mkdir(parents=True, exist_ok=True)
            (pdir / "outputs").mkdir(parents=True, exist_ok=True)
            n = 2000
            s0 = [0.3 * math.sin(2 * math.pi * 200 * k / SR) for k in range(n)]
            s1 = [0.3 * math.sin(2 * math.pi * 300 * k / SR) for k in range(n)]
            s2 = [0.3 * math.sin(2 * math.pi * 400 * k / SR) for k in range(n)]
            _write_wav(pdir / "in0.wav", [s0, s0])
            _write_wav(pdir / "in1.wav", [s1, s1])
            _write_wav(pdir / "in2.wav", [s2, s2])
            srcs = {0: "in0.wav", 1: "in1.wav", 2: "in2.wav"}
            tol = 3 * (1 / 32768.0) + 1e-8
            for sel, expect in [(1, s0), (2, s1), (3, s2)]:
                out, _ = _mux_run(client, name, srcs, select=sel, inputs=3,
                                  ramp_ms=0.0, out_name=f"o{sel}.wav")
                err = max(abs(a - b) for a, b in zip(out[0], expect))
                assert err < tol, (sel, err)
        finally:
            client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(_SKIP, reason="n_way_mux not built")
def test_mux_variable_pin_bind():
    name = f"mux_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            pdir = ROOT / "workspace" / name
            pdir.mkdir(parents=True, exist_ok=True)
            (pdir / "outputs").mkdir(parents=True, exist_ok=True)
            s0 = [0.2 * math.sin(2 * math.pi * 220 * k / SR) for k in range(1000)]
            _write_wav(pdir / "in0.wav", [s0, s0])
            out, rj = _mux_run(client, name, {0: "in0.wav"}, select=1,
                               inputs=3, ramp_ms=0.0, out_name="out.wav")
            assert rj["status"] == "ok"
            tol = 3 * (1 / 32768.0) + 1e-8
            err = max(abs(a - b) for a, b in zip(out[0], s0))
            assert err < tol, err
        finally:
            client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(_SKIP, reason="n_way_mux not built")
def test_mux_select_clamped_to_inputs():
    """select 越界（> inputs）应钳到最后一路；0/负数钳到第 1 路。"""
    name = f"mux_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            pdir = ROOT / "workspace" / name
            pdir.mkdir(parents=True, exist_ok=True)
            (pdir / "outputs").mkdir(parents=True, exist_ok=True)
            n = 2000
            s0 = [0.4 * math.sin(2 * math.pi * 200 * k / SR) for k in range(n)]
            s1 = [0.4 * math.sin(2 * math.pi * 300 * k / SR) for k in range(n)]
            _write_wav(pdir / "in0.wav", [s0, s0])
            _write_wav(pdir / "in1.wav", [s1, s1])
            tol = 3 * (1 / 32768.0) + 1e-8
            out, _ = _mux_run(client, name, {0: "in0.wav", 1: "in1.wav"},
                              select=7, inputs=2, ramp_ms=0.0, out_name="hi.wav")
            err = max(abs(a - b) for a, b in zip(out[0], s1))
            assert err < tol, ("clamp-high", err)
            out, _ = _mux_run(client, name, {0: "in0.wav", 1: "in1.wav"},
                              select=0, inputs=2, ramp_ms=0.0, out_name="lo.wav")
            err = max(abs(a - b) for a, b in zip(out[0], s0))
            assert err < tol, ("clamp-low", err)
        finally:
            client.delete(f"/api/projects/{name}")
