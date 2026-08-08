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
        compiled = client.post(f"/api/projects/{name}/compile")
        assert compiled.status_code == 200, compiled.text
        assert compiled.json()["id_map"], "编译响应应携带 id_map（UI 显示 0x ID 用）"

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
    probe_id = cases[2][0]
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

    # 模块包：用途=TUNE、形式=MODULE、槽 0xFFFF；动态路径按模块切片 → 有真实基址
    mod = next(m for m in plan["modules"] if m["path"] == "front")
    mod_id = (0x1 << 28) | (mod["id"] << 16) | 0xFFFF
    out = subprocess.run(
        [str(exe), str(plan_path), str(comps), "--resolve", str(mod_id)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0, out.stderr
    parts = out.stdout.strip().split()
    assert parts[3] == "MODULE" and "slot=65535" in out.stdout
    assert "base=0000000000000000" not in out.stdout  # 模块连续分配后基址非空

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

    # 按 ID 实时控制：RW 写 RTC 参数 → RR 读回；PROBE 拒写；RWB 直写 bulk
    rtc_id = by_key[("front__trim", "gain_db")]["id"]
    out = subprocess.run(
        [str(exe), str(plan_path), str(comps),
         "--rw", str(rtc_id), "-6", "--rr", str(rtc_id)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0, out.stderr
    assert "OK RW" in out.stdout
    rv = [l for l in out.stdout.splitlines() if l.startswith("RVALUE")]
    assert rv and abs(float(rv[0].split()[-1]) + 6.0) < 1e-3

    out = subprocess.run(
        [str(exe), str(plan_path), str(comps), "--rw", str(probe_id), "1"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0 and "ERR RW" in out.stdout  # PROBE 拒写

    out = subprocess.run(
        [str(exe), str(plan_path), str(comps),
         "--rwb", str(bulk_id), "5", "0.1", "0.2", "0.3", "0.4", "0.5"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0 and "OK RWB" in out.stdout, out.stderr

    # 双 bank：写影子未提交 → 读回仍是旧值；跑 1 块（块边界提交）→ 读到新值
    new_vals = ["1", "2", "3", "4", "5"]
    out = subprocess.run(
        [str(exe), str(plan_path), str(comps),
         "--rwb", str(bulk_id), "5", *new_vals, "--rgb", str(bulk_id)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0 and "OK RWB" in out.stdout, out.stderr
    rv = [l for l in out.stdout.splitlines() if l.startswith("BULKVALUE ")]
    assert rv and float(rv[0].split()[2]) != 1.0, "影子未提交前应读到旧值"

    out = subprocess.run(
        [str(exe), str(plan_path), str(comps),
         "--rwb", str(bulk_id), "5", *new_vals, "--run", "1", "--rgb", str(bulk_id)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0, out.stderr
    rv = [l for l in out.stdout.splitlines() if l.startswith("BULKVALUE ")]
    assert rv and [float(v) for v in rv[0].split()[2:]] == [1.0, 2.0, 3.0, 4.0, 5.0]

    # GETBULK 按 (node, key) 读回 active
    out = subprocess.run(
        [str(exe), str(plan_path), str(comps),
         "--getbulk", "front__eq_bank__bq", "bq0.coefs"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
    )
    assert out.returncode == 0 and out.stdout.strip().startswith("BULKVALUE "), out.stderr
    assert len(out.stdout.strip().split()) == 7  # BULKVALUE 0xID + 5 个值
