"""生成路径 BULK 双 bank（可选）测试：影子+块边界提交 / off 直写即时生效。"""

from __future__ import annotations

import json
import subprocess
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.builder import run_cmake_with_msvc_env
from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]
_CREATED: list[str] = []
NEW_VALS = ["1", "2", "3", "4", "5"]


def _new_name() -> str:
    name = f"gendb_{uuid.uuid4().hex[:8]}"
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


def _make_project(client, name: str, double_bank: str) -> Path:
    resp = client.post(
        "/api/projects", json={"name": name, "from_example": "dsp_model_reference"}
    )
    assert resp.status_code == 201, resp.text
    doc = client.get(f"/api/projects/{name}").json()
    doc["double_bank"] = double_bank
    assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    assert client.post(f"/api/projects/{name}/generate").status_code == 200
    return ROOT / "workspace" / name


def _build(gen_dir: Path, cwd: Path) -> Path:
    build_dir = gen_dir / "build"
    args = ["cmake", "-S", str(gen_dir), "-B", str(build_dir), "-G", "Ninja"]
    cache = ROOT / "build" / "CMakeCache.txt"
    for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("CMAKE_C_COMPILER:") and "=" in line:
            args.append(f"-DCMAKE_C_COMPILER={line.split('=', 1)[1]}")
    cfg = run_cmake_with_msvc_env(args, cwd=cwd, build_dir=ROOT / "build")
    assert cfg.returncode == 0, cfg.stderr
    b = run_cmake_with_msvc_env(
        ["cmake", "--build", str(build_dir)], cwd=cwd, build_dir=ROOT / "build"
    )
    assert b.returncode == 0, b.stderr
    return build_dir / "orpheus_generated_app.exe"


def _run(exe: Path, cwd: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(exe), *args], capture_output=True, text=True,
        encoding="utf-8", errors="replace", cwd=cwd,
    )


def test_generated_double_bank_auto() -> None:
    name = _new_name()
    with TestClient(create_app(ROOT)) as client:
        cwd = _make_project(client, name, "auto")
    gen = cwd / "generated"
    control = (gen / "src" / "orpheus_control.c").read_text(encoding="utf-8")
    assert "g_db_front__eq_bank__bq_bq0_coefs" in control  # auto=按组件声明 → 产出影子
    exe = _build(gen, cwd)

    # 写影子未提交 → 读回旧值（biquad_bank prepare 计算的 peaking 系数，非 1..5）
    r = _run(
        exe, cwd, "0",
        "--write-bulk", "front__eq_bank__bq", "bq0.coefs", "5", *NEW_VALS,
        "--read-bulk", "front__eq_bank__bq", "bq0.coefs",
    )
    assert "OK WRITEBULK" in r.stdout, r.stderr
    rv = [l for l in r.stdout.splitlines() if l.startswith("BULKVALUE ")]
    assert rv and float(rv[0].split()[3]) != 1.0, "影子未提交前应读到旧值"

    # 跑 1 块（块边界提交）→ 新值
    r = _run(
        exe, cwd, "0",
        "--write-bulk", "front__eq_bank__bq", "bq0.coefs", "5", *NEW_VALS,
        "--run", "1", "--read-bulk", "front__eq_bank__bq", "bq0.coefs",
    )
    rv = [l for l in r.stdout.splitlines() if l.startswith("BULKVALUE ")]
    assert rv and [float(v) for v in rv[0].split()[3:]] == [1.0, 2.0, 3.0, 4.0, 5.0]

    # 按 32 位 ID 写/读（部署控制通道）
    plan = json.loads((cwd / "project.plan.json").read_text(encoding="utf-8"))
    e = next(
        x for x in plan["id_map"]
        if x["node"] == "front__eq_bank__bq" and x["key"] == "bq0.coefs"
    )
    r = _run(
        exe, cwd, "0",
        "--write-bulk-id", str(e["id"]), "5", *NEW_VALS,
        "--run", "1", "--read-bulk-id", str(e["id"]),
    )
    rv = [l for l in r.stdout.splitlines() if l.startswith("BULKVALUE ")]
    assert rv and [float(v) for v in rv[0].split()[2:]] == [1.0, 2.0, 3.0, 4.0, 5.0]


def test_generated_double_bank_off() -> None:
    name = _new_name()
    with TestClient(create_app(ROOT)) as client:
        cwd = _make_project(client, name, "off")
    gen = cwd / "generated"
    control = (gen / "src" / "orpheus_control.c").read_text(encoding="utf-8")
    assert "g_db_front__eq_bank__bq_bq0_coefs" not in control  # off → 无影子（省内存）
    exe = _build(gen, cwd)

    # 直写 active → 即时生效（无需等块边界）
    r = _run(
        exe, cwd, "0",
        "--write-bulk", "front__eq_bank__bq", "bq0.coefs", "5", *NEW_VALS,
        "--read-bulk", "front__eq_bank__bq", "bq0.coefs",
    )
    rv = [l for l in r.stdout.splitlines() if l.startswith("BULKVALUE ")]
    assert rv and [float(v) for v in rv[0].split()[3:]] == [1.0, 2.0, 3.0, 4.0, 5.0]
