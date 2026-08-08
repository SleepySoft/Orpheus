"""Runtime resolve（内存透明）测试：ID → 用途/形式/类型/长度/基址/偏移。"""

from __future__ import annotations

import json
import subprocess
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]
_CREATED: list[str] = []


def _new_name() -> str:
    name = f"resolve_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    return name


@pytest.fixture(autouse=True)
def _cleanup_projects():
    yield
    if _CREATED:
        with TestClient(create_app(ROOT)) as client:
            for name in _CREATED:
                try:
                    client.delete(f"/api/projects/{name}")
                except Exception:
                    pass
    _CREATED.clear()


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_runtime_resolve_and_map() -> None:
    name = _new_name()
    with TestClient(create_app(ROOT)) as client:
        resp = client.post(
            "/api/projects", json={"name": name, "from_example": "dsp_model_reference"}
        )
        assert resp.status_code == 201, resp.text
        assert client.post(f"/api/projects/{name}/compile").status_code == 200

    plan_path = ROOT / "workspace" / name / "project.plan.json"
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    by_key = {(e["node"], e["key"]): e for e in plan["id_map"]}
    comps = ROOT / "build" / "components"
    exe = ROOT / "build" / "orpheus_runtime.exe"
    cwd = ROOT / "workspace" / name

    # 数据点 resolve：RTC 实时参数 / TUNE 滤波器 / PROBE 探针
    cases = [
        (by_key[("front__trim", "gain_db")]["id"], "RTC"),
        (by_key[("front__eq_bank__bq", "fc0")]["id"], "TUNE"),
        (by_key[("front__mon", "rms")]["id"], "PROBE"),
    ]
    for id_, kind in cases:
        out = subprocess.run(
            [str(exe), str(plan_path), str(comps), "--resolve", str(id_)],
            capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
        )
        assert out.returncode == 0, out.stderr
        line = out.stdout.strip()
        assert line.startswith("RESOLVED 0x"), line
        parts = line.split()
        assert parts[1].lower() == f"0x{id_:x}"
        assert parts[2] == kind
        assert "base=" in line and "base=0000000000000000" not in line  # 数据点有真实地址
        assert "bytes=" in line and "offset=" in line

    # bulk 形式：bq0.coefs = 5 floats = 20 B
    bulk_id = by_key[("front__eq_bank__bq", "bq0.coefs")]["id"]
    out = subprocess.run(
        [str(exe), str(plan_path), str(comps), "--resolve", str(bulk_id)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0, out.stderr
    parts = out.stdout.strip().split()
    assert parts[3] == "BULK" and "bytes=20" in out.stdout

    # 模块包：用途=TUNE、形式=MODULE、槽 0xFFFF；动态路径未连续分配 → base=0x0
    mod = next(m for m in plan["modules"] if m["path"] == "front")
    mod_id = (0x1 << 28) | (mod["id"] << 16) | 0xFFFF
    out = subprocess.run(
        [str(exe), str(plan_path), str(comps), "--resolve", str(mod_id)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0, out.stderr
    parts = out.stdout.strip().split()
    assert parts[3] == "MODULE" and "slot=65535" in out.stdout
    assert "base=0000000000000000" in out.stdout  # 动态路径模块未连续分配 → 无基址

    # MAP 全表：数据点 + 模块包
    out = subprocess.run(
        [str(exe), str(plan_path), str(comps), "--map"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0, out.stderr
    lines = [l for l in out.stdout.splitlines() if l.startswith("RESOLVED ")]
    # 数据点（个别 manifest 参数未注册为运行槽则 resolve 跳过）+ 模块包
    assert len(lines) >= len(plan["id_map"]) - 2 + 6
    assert any(l.split()[3] == "MODULE" for l in lines)
    assert any("node=front__trim key=gain_db" in l for l in lines)
