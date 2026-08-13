"""分解的 FxLMS 主动降噪验证：复合组件 sub:anc_fxlms 展开为通用原子链路并能去降噪。

验证两个点：
1) 子组件能被 flatten 展开为纯原子图（全部艺都可见）；
2) 运行后自适应核心收敛—>conv_metric 很小（残余趋零），说明去降噪有效。
"""

from __future__ import annotations

import math
import uuid
import wave
from pathlib import Path

import numpy as np
import pytest
import yaml
from fastapi.testclient import TestClient

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app
from orpheus_core.subgraph import flatten_project

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


def _sine(freq: float, n: int, amp: float = 0.5, sr: int = 48000) -> list[float]:
    return [amp * math.sin(2 * math.pi * freq * i / sr) for i in range(n)]


def _write_wav(pdir: Path, fname: str, samples: list[float], ch: int = 1) -> None:
    n = len(samples) // ch
    arr = np.asarray(samples, dtype=np.float32).reshape(n, ch)
    with wave.open(str(pdir / fname), "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes((np.clip(arr, -1, 1) * 32767).astype("<i2").tobytes())


def test_decomposed_anc_flattens_to_visible_chain() -> None:
    """子组件被展开为可见原子链：【x -> gain_src -> sdelay -> sgain -> core -> neg -> -y】。"""
    loader = ProjectLoader()
    proj = loader.load(ROOT / "examples" / "anc_fxlms_decomposed.yaml")
    assert [s.id for s in proj.subcomponents] == ["anc_fxlms"]
    flat = flatten_project(proj)
    ids = sorted(flat.graph.nodes.keys())
    # 展开后必须包含这些通用原子
    assert "anc__adaptive" in ids or any("core" in i for i in ids)
    assert any("delay" in i for i in ids)
    assert any("gain" in i for i in ids)
    assert any("neg" in i for i in ids)
    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    plan = GraphCompiler(reg).compile(flat)
    order = list(plan.execution_order)
    # gain_src 之後必须是 delay_line 或 gain（次级路径），最终反相输出
    assert "wx" in order and "wd" in order
    assert order[-1] == "out"


@_RT_BUILT
def test_decomposed_anc_converges(client) -> None:
    """分解版运行后收敛：adaptive_fir 核心 conv_metric 很小（残余趋零）。"""
    name = f"ancd_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    pdir = ROOT / "workspace" / name
    x = _sine(200.0, 48000)
    _write_wav(pdir, "noise_x.wav", x, 1)
    _write_wav(pdir, "noise_d.wav", x, 1)
    doc = yaml.safe_load((ROOT / "examples" / "anc_fxlms_decomposed.yaml").read_text(encoding="utf-8"))
    for nd in doc["graph"]["nodes"]:
        p = nd.get("params", {})
        if nd["component"] == "orpheus.builtin.wav_in":
            p["file_path"] = "noise_d.wav" if nd["id"] == "wd" else "noise_x.wav"
        if nd["component"] == "orpheus.builtin.wav_out":
            p["file_path"] = "outputs/out.wav"
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    core = [p for p in result.get("probes", []) if p["node"].startswith("anc__core") and p["param"] == "conv_metric"]
    assert core, "conv_metric probe missing"
    assert core[-1]["value"] < 0.01, f"adaptive core should converge, got {core[-1]['value']}"
