"""per-channel 组件（delay_line / iir_bank）数值正确性验证。"""

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


def _run(client, nodes, conns, samples, ch: int = 1, block_size: int = 128) -> list[float]:
    name = f"pc_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    _write_input(pdir, samples, ch)
    doc = client.get(f"/api/projects/{name}").json()
    doc["sample_rate"] = 48000
    doc["block_size"] = block_size
    doc["graph"] = {"nodes": nodes, "connections": conns}
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    out = pdir / "outputs" / "out.wav"
    with wave.open(str(out), "rb") as w:
        data = w.readframes(w.getnframes())
    return [v / 32767.0 for v in struct.unpack(f"<{len(data) // 2}h", data)]


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
def test_delay_line_per_channel(client) -> None:
    """每通道独立延迟：ch0 延迟 3 样本，ch1 延迟 5 样本。"""
    nodes = _io_nodes("delay_line", {"max_delay_samples": 16, "delays_samples": "3,5"}, ch=2)
    samples = [1.0, 1.0] + [0.0, 0.0] * 31  # 64 样本，双通道
    vals = _run(client, nodes, _chain([n["id"] for n in nodes]), samples, ch=2)

    # 输出为交错格式
    ch0 = vals[0::2]
    ch1 = vals[1::2]
    assert abs(ch0[0]) < 1e-3 and abs(ch0[3] - 1.0) < 2e-3
    assert abs(ch1[0]) < 1e-3 and abs(ch1[5] - 1.0) < 2e-3


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_iir_bank_per_channel(client) -> None:
    """per_channel 模式：ch0 增益 0.5，ch1 增益 2.0，验证每通道独立系数。"""
    nodes = _io_nodes(
        "iir_bank",
        {
            "num_stages": 1,
            "coefs_mode": "per_channel",
            "coefs": "0.5,0,0,0,0,2.0,0,0,0,0",
        },
        ch=2,
    )
    samples = [0.3, 0.3] * 128  # 256 样本 = 128 帧，填满默认 block_size
    vals = _run(client, nodes, _chain([n["id"] for n in nodes]), samples, ch=2)
    ch0 = vals[0::2]
    ch1 = vals[1::2]
    assert abs(ch0[-1] - 0.15) < 2e-3
    assert abs(ch1[-1] - 0.60) < 2e-3


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_iir_bank_shared_backward_compatible(client) -> None:
    """shared 模式保持原有行为：所有通道共用一组系数。"""
    nodes = _io_nodes(
        "iir_bank",
        {
            "num_stages": 1,
            "coefs_mode": "shared",
            "coefs": "0.5,0,0,0,0",
        },
        ch=2,
    )
    samples = [0.3, 0.3] * 128
    vals = _run(client, nodes, _chain([n["id"] for n in nodes]), samples, ch=2)
    ch0 = vals[0::2]
    ch1 = vals[1::2]
    assert abs(ch0[-1] - 0.15) < 2e-3
    assert abs(ch1[-1] - 0.15) < 2e-3


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_iir_bank_df2t_impulse_response(client) -> None:
    """非平凡一阶递归：y[n]=x[n]+0.5*y[n-1]，验证 DF2T 状态更新。"""
    nodes = _io_nodes(
        "iir_bank",
        {
            "num_stages": 1,
            "coefs_mode": "shared",
            "coefs": "1,0,0,-0.5,0",
        },
        ch=1,
    )
    samples = [0.5] + [0.0] * 127
    vals = _run(client, nodes, _chain([n["id"] for n in nodes]), samples, ch=1)
    expected = [0.5 * (0.5 ** index) for index in range(8)]
    assert vals[:8] == pytest.approx(expected, abs=2e-3)


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_iir_bank_matches_bands_hpf_lpf_pooliir_conversion(client) -> None:
    hpf = [
        0.920707226, -0.920707226, 0.0, -0.95020926, 0.0,
        1.0, -2.0, 1.0, -1.9182049, 0.920707285,
        1.0, -2.0, 1.0, -1.96638691, 0.968952119,
    ]
    lpf = [
        9.987561e-9, 9.987561e-9, 0.0, -0.95020926, 0.0,
        1.0, 2.0, 1.0, -1.9182049, 0.920707285,
        1.0, 2.0, 1.0, -1.96638691, 0.968952119,
    ]
    nodes = _io_nodes(
        "iir_bank",
        {
            "num_stages": 3,
            "coefs_mode": "per_channel",
            "coefs": ",".join(str(value) for value in [*hpf, *lpf]),
        },
        ch=2,
    )
    vals = _run(
        client,
        nodes,
        _chain([node["id"] for node in nodes]),
        [0.5, 0.5] + [0.0, 0.0] * 127,
        ch=2,
    )

    def cascade(coefficients: list[float]) -> list[float]:
        states = [[0.0, 0.0] for _ in range(3)]
        output = []
        for frame in range(128):
            sample = 0.5 if frame == 0 else 0.0
            for stage in range(3):
                b0, b1, b2, a1, a2 = coefficients[stage * 5:stage * 5 + 5]
                z1, z2 = states[stage]
                filtered = b0 * sample + z1
                states[stage][0] = b1 * sample + z2 - a1 * filtered
                states[stage][1] = b2 * sample - a2 * filtered
                sample = filtered
            output.append(sample)
        return output

    assert vals[0::2][:128] == pytest.approx(cascade(hpf), abs=2e-3)
    assert vals[1::2][:128] == pytest.approx(cascade(lpf), abs=2e-3)
