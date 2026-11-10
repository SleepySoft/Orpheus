"""Runtime resolve（内存透明）测试：ID → 用途/形式/类型/长度/基址/偏移。"""

from __future__ import annotations

import json
import shutil
import subprocess
import uuid
from dataclasses import asdict
from pathlib import Path

import pytest
from editor_client import EditorClient as TestClient

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]
_CREATED: list[str] = []


def _new_name() -> str:
    name = f"resolve_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    return name


def _assert_example_map(plan: dict, lines: list[str]) -> None:
    # 编译期目录包含初始化配置与条件槽；MAP 只列出当前已注册的数据点。
    unavailable = {("post__fir", "coefficients")}
    unavailable.update(
        (node, "gains")
        for node, config in plan["node_configs"].items()
        if config["component"] == "orpheus.builtin.gain"
        and config["params"].get("gain_mode", 0) != 2
    )
    declared = {(entry["node"], entry["key"]): entry["id"] for entry in plan["id_map"]}
    assert unavailable <= declared.keys()
    expected = {data_id for key, data_id in declared.items() if key not in unavailable}
    expected.update(
        (0x1 << 28) | (module["id"] << 16) | 0xFFFF
        for module in plan["modules"] if module["path"]
    )
    actual = [int(line.split()[1], 16) for line in lines]
    assert len(actual) == len(set(actual)), "MAP 不应重复返回同一 ID"
    assert set(actual) == expected


@pytest.mark.parametrize("gain_mode", [0, 1, 2])
@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_runtime_map_tracks_registered_gain_mode(tmp_path, gain_mode):
    registry = Registry([ROOT / "components"])
    registry.scan()
    project = flatten_project(
        ProjectLoader(registry).load(ROOT / "examples" / "dsp_model_reference.yaml")
    )
    for node in project.graph.nodes.values():
        if node.component == "orpheus.builtin.gain":
            node.params["gain_mode"] = gain_mode
    plan = asdict(GraphCompiler(registry).compile(project))
    path = tmp_path / "plan.json"
    path.write_text(json.dumps(plan), encoding="utf-8")
    shutil.copy2(ROOT / "examples" / "test_input.wav", tmp_path / "test_input.wav")
    result = subprocess.run(
        [str(ROOT / "build" / "orpheus_runtime.exe"), str(path),
         str(ROOT / "build" / "components"), "--map"],
        cwd=tmp_path, capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 0, result.stderr
    _assert_example_map(plan, [line for line in result.stdout.splitlines()
                               if line.startswith("RESOLVED ")])


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
    assert by_key[("front__eq_bank__bq", "bq0.coefs")]["double_bank"] is True  # auto=按组件声明
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
    _assert_example_map(plan, lines)
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

    # 工程级 double_bank=off：关闭双缓冲 → 直写 active、即时生效（部署省内存路径）
    name2 = _new_name()
    with TestClient(create_app(ROOT)) as client:
        resp2 = client.post(
            "/api/projects", json={"name": name2, "from_example": "dsp_model_reference"}
        )
        assert resp2.status_code == 201, resp2.text
        doc2 = client.get(f"/api/projects/{name2}").json()
        doc2["double_bank"] = "off"
        assert client.put(f"/api/projects/{name2}", json=doc2).status_code == 200
        assert client.post(f"/api/projects/{name2}/compile").status_code == 200
    plan2_path = ROOT / "workspace" / name2 / "project.plan.json"
    plan2 = json.loads(plan2_path.read_text(encoding="utf-8"))
    e2 = next(
        x for x in plan2["id_map"]
        if x["node"] == "front__eq_bank__bq" and x["key"] == "bq0.coefs"
    )
    assert e2["double_bank"] is False
    cwd2 = ROOT / "workspace" / name2
    out = subprocess.run(
        [str(exe), str(plan2_path), str(comps),
         "--rwb", str(e2["id"]), "5", *new_vals, "--rgb", str(e2["id"])],
        capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd2,
    )
    assert out.returncode == 0 and "OK RWB" in out.stdout, out.stderr
    rv = [l for l in out.stdout.splitlines() if l.startswith("BULKVALUE ")]
    assert rv and [float(v) for v in rv[0].split()[2:]] == [1.0, 2.0, 3.0, 4.0, 5.0], \
        "off 模式应直写 active 即时生效"
