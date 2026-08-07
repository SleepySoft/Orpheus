"""嵌入 I/O 占位组件（embed_in / embed_out）与生成代码 platform_io 适配测试。

覆盖：
- embed 图编译（embed 时钟域）、动态离线运行（静音）；
- 生成工程含 platform_io.c 模板并可构建运行；
- 用户在 USER CODE 段填充输入后，生成工程输出数值正确（易于填充的验证）。
"""

from __future__ import annotations

import struct
import subprocess
import uuid
import wave
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.builder import run_cmake_with_msvc_env
from orpheus_core.parameter_catalog import build_catalog
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]

GAIN_MINUS_6 = 10 ** (-6.0 / 20.0)  # -6 dB 线性增益
_CREATED: list[str] = []


def _embed_doc(with_wav_out: bool = False) -> dict:
    nodes = [
        {
            "id": "in",
            "component": "orpheus.builtin.embed_in",
            "params": {"channels": 2, "sample_rate": 48000},
            "position": {"x": 0, "y": 0},
        },
        {
            "id": "g",
            "component": "orpheus.builtin.gain",
            "params": {"gain_db": -6.0, "channels": 2},
            "position": {"x": 200, "y": 0},
        },
    ]
    conns = [{"from": "in:out", "to": "g:in"}]
    if with_wav_out:
        nodes.append(
            {
                "id": "out",
                "component": "orpheus.builtin.wav_out",
                "params": {"file_path": "outputs/out.wav", "channels": 2, "sample_rate": 48000},
                "position": {"x": 400, "y": 0},
            }
        )
        conns.append({"from": "g:out", "to": "out:in"})
    else:
        nodes.append(
            {
                "id": "sink",
                "component": "orpheus.builtin.embed_out",
                "params": {"channels": 2, "sample_rate": 48000},
                "position": {"x": 400, "y": 0},
            }
        )
        conns.append({"from": "g:out", "to": "sink:in"})
    return {"nodes": nodes, "connections": conns}


@pytest.fixture()
def client():
    with TestClient(create_app(ROOT)) as c:
        yield c


def _create_embed_project(client, with_wav_out: bool) -> str:
    name = f"embed_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    assert client.post("/api/projects", json={"name": name}).status_code == 201
    doc = client.get(f"/api/projects/{name}").json()
    doc["graph"] = _embed_doc(with_wav_out)
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    return name


@pytest.fixture(autouse=True)
def _cleanup_projects(client):
    """测试结束后删除创建的 workspace 工程，避免污染 UI 工程列表。"""
    yield
    for name in _CREATED:
        try:
            client.delete(f"/api/projects/{name}")
        except Exception:
            pass
    _CREATED.clear()


def test_embed_graph_compiles(client) -> None:
    name = _create_embed_project(client, with_wav_out=True)
    resp = client.post(f"/api/projects/{name}/compile")
    assert resp.status_code == 200, resp.text
    assert resp.json()["execution_order"] == ["in", "g", "out"]


def test_embed_catalog_kinds(client) -> None:
    """embed 组件作为数据点进入参数面板分类：channels/sample_rate=setting，underruns=probe。"""
    name = _create_embed_project(client, with_wav_out=False)
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = ProjectLoader().load(ROOT / "workspace" / name / "project.yaml")
    entries = {e.flat_id: e for e in build_catalog(project, registry)}
    in_entry = entries["in"]
    assert {"channels", "sample_rate"} <= {p["id"] for p in in_entry.params_of("setting")}
    assert [p["id"] for p in in_entry.params_of("probe")] == ["underruns"]
    out_entry = entries["sink"]
    assert {"channels", "sample_rate"} <= {p["id"] for p in out_entry.params_of("setting")}


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_embed_dynamic_run_silence(client) -> None:
    """动态离线运行：未填充的 embed_in 输出静音（并计欠载），wav 全零。"""
    name = _create_embed_project(client, with_wav_out=True)
    resp = client.post(f"/api/projects/{name}/run")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    out = ROOT / "workspace" / name / "outputs" / "out.wav"
    with wave.open(str(out), "rb") as w:
        data = w.readframes(w.getnframes())
    vals = struct.unpack(f"<{len(data) // 4}f", data)
    assert len(vals) > 0 and all(abs(v) < 1e-9 for v in vals)
    assert any(p["node"] == "in" and p["param"] == "underruns" and p["value"] > 0 for p in result["probes"])


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_embed_generated_project_builds_and_runs(client) -> None:
    """生成工程含 platform_io.c 模板，默认（未填充）可构建运行、退出码 0。"""
    name = _create_embed_project(client, with_wav_out=False)
    resp = client.post(f"/api/projects/{name}/run_generated")
    assert resp.status_code == 200, resp.text
    result = resp.json()
    assert result["status"] == "ok", result["stderr"]
    gen = ROOT / "workspace" / name / "generated"
    text = (gen / "src" / "platform_io.c").read_text(encoding="utf-8")
    assert "orpheus_platform_io_pre_block" in text
    assert "/* USER CODE BEGIN */" in text
    assert "orpheus_embed_in_state_in" in text


@pytest.mark.skipif(
    not (ROOT / "build" / "CMakeCache.txt").exists(),
    reason="main build not configured",
)
def test_embed_fill_numerics(client) -> None:
    """「易于填充」验证：用户只改 platform_io.c 的 pre_block USER CODE 段，
    生成工程即输出 ramp × gain 的 WAV。"""
    name = _create_embed_project(client, with_wav_out=True)
    resp = client.post(f"/api/projects/{name}/generate")
    assert resp.status_code == 200, resp.text
    gen_dir = ROOT / "workspace" / name / "generated"

    # 在 pre_block 的 USER CODE 段填入 ramp（L=+0.25, R=-0.25）
    platform = gen_dir / "src" / "platform_io.c"
    text = platform.read_text(encoding="utf-8")
    marker = "void orpheus_platform_io_pre_block(void) {"
    start = text.index(marker)
    begin = text.index("/* USER CODE BEGIN */", start) + len("/* USER CODE BEGIN */")
    end = text.index("/* USER CODE END */", start)
    body = (
        '    for (int i = 0; i < 128 * 2; ++i) g_embed_in_in[i] = (i % 2 == 0) ? 0.25f : -0.25f;\n'
        '    in->src_frames = 128;\n'
    )
    platform.write_text(text[:begin] + "\n" + body + text[end:], encoding="utf-8")

    # 构建生成工程（复用主构建工具链，与 run_generated 相同逻辑）
    build_dir = gen_dir / "build"
    configure_args = ["cmake", "-S", str(gen_dir), "-B", str(build_dir), "-G", "Ninja"]
    cache = ROOT / "build" / "CMakeCache.txt"
    for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("CMAKE_C_COMPILER:") and "=" in line:
            configure_args.append(f"-DCMAKE_C_COMPILER={line.split('=', 1)[1]}")
    cwd = ROOT / "workspace" / name
    configure = run_cmake_with_msvc_env(configure_args, cwd=cwd, build_dir=ROOT / "build")
    assert configure.returncode == 0, configure.stderr
    build = run_cmake_with_msvc_env(
        ["cmake", "--build", str(build_dir)], cwd=cwd, build_dir=ROOT / "build"
    )
    assert build.returncode == 0, build.stderr
    exe = build_dir / "orpheus_generated_app.exe"
    subprocess.run([str(exe), "200"], cwd=cwd, check=True)

    out = cwd / "outputs" / "out.wav"
    with wave.open(str(out), "rb") as w:
        frames = w.getnframes()
        data = w.readframes(frames)
    assert frames == 200 * 128
    # wav_out 落盘为 16-bit PCM：int16 = sample * 32767
    vals = struct.unpack(f"<{len(data) // 2}h", data)
    expected = int(round(0.25 * GAIN_MINUS_6 * 32767))
    assert abs(vals[0] - expected) <= 1  # 第 0 帧左声道
    assert abs(vals[1] + expected) <= 1  # 第 0 帧右声道
    assert abs(vals[-2] - expected) <= 1
    assert abs(vals[-1] + expected) <= 1
