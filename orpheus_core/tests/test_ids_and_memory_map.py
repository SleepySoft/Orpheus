"""数据 ID（32 位单 ID）与生成代码 ID map / 内存布局测试。

覆盖：
- plan.modules：模块树 DFS 分配稳定 id、模块内叶子槽连续、嵌套模块归属正确；
- 生成产物：orpheus_ids.h（宏唯一、单叶子模块=公司风格命名）、orpheus_id_map.c
  （offsetof 精确偏移）、memory_map.md、ABI 的 CUSTOM/Reserved kind；
- 嵌套子模块生成工程（模块嵌套 arena + id_map）可编译运行。
"""

from __future__ import annotations

import shutil
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app
from orpheus_core.subgraph import flatten_project

ROOT = Path(__file__).resolve().parents[2]
_CREATED: list[str] = []


def _new_name() -> str:
    name = f"ids_{uuid.uuid4().hex[:8]}"
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


@pytest.fixture(scope="module")
def registry() -> Registry:
    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    return reg


@pytest.fixture(scope="module")
def plan(registry: Registry):
    proj = ProjectLoader().load(ROOT / "examples" / "dsp_model_reference.yaml")
    return GraphCompiler(registry).compile(flatten_project(proj))


def test_module_layout_stable_and_contiguous(plan) -> None:
    modules = {m["path"]: m for m in plan.modules}
    assert modules[""]["id"] == 0
    assert modules["front"]["id"] > 0
    # front 的直接叶子不含 front__eq_bank__bq（它在子模块里）
    front_leaves = {l["node"] for l in modules["front"]["leaves"]}
    assert "front__eq_bank__bq" not in front_leaves
    assert "front__trim" in front_leaves
    assert modules["front__eq_bank"]["leaves"][0]["node"] == "front__eq_bank__bq"
    # 模块内叶子槽连续（执行序）
    assert [l["slot"] for l in modules["front"]["leaves"]] == [0, 1, 2]


def test_module_layout_deterministic(registry: Registry, plan) -> None:
    proj = ProjectLoader().load(ROOT / "examples" / "dsp_model_reference.yaml")
    plan2 = GraphCompiler(registry).compile(flatten_project(proj))
    assert [m["path"] for m in plan.modules] == [m["path"] for m in plan2.modules]
    assert [m["id"] for m in plan.modules] == [m["id"] for m in plan2.modules]


def test_generated_ids_macros_and_map(tmp_path: Path, registry: Registry, plan) -> None:
    out = tmp_path / "gen"
    CodeGenerator(registry, ROOT).generate(plan, out)

    ids = (out / "include" / "orpheus_ids.h").read_text(encoding="utf-8")
    assert "ORPHEUS_MODULE_Front" in ids
    assert "ORPHEUS_TUNE_FrontEqBankFc0" in ids  # 单叶子模块 = 公司风格 模块+参数
    assert "ORPHEUS_CHAR_COUNT_FrontEqBankFc0" in ids
    # RTC = 实时可调参数（smoothed）；TUNE = restart/配置参数
    assert "ORPHEUS_RTC_FrontTrimGainDb" in ids   # gain_db smoothed → RTC
    assert "ORPHEUS_RTC_FrontMuteMute" in ids     # mute smoothed → RTC
    assert "ORPHEUS_TUNE_FrontTrimChannels" in ids  # channels restart → TUNE
    # 多叶子模块带叶子名，避免顶层同名参数冲突
    assert "ORPHEUS_TUNE_WavInChannels" in ids
    assert "ORPHEUS_TUNE_OutMonChannels" in ids
    names = [
        l.split()[1]
        for l in ids.splitlines()
        if l.startswith("#define ORPHEUS_") and "CHAR_COUNT" not in l
    ]
    assert len(names) == len(set(names)), "宏名重复"

    abi = (ROOT / "orpheus_abi" / "include" / "orpheus_abi.h").read_text(encoding="utf-8")
    assert "ORPHEUS_ID_RTC" in abi
    assert "ORPHEUS_ID_CUSTOM" in abi
    assert "ORPHEUS_ID_RESERVED_7" in abi
    assert "ORPHEUS_ID_MAKE" in abi

    idc = (out / "src" / "orpheus_id_map.c").read_text(encoding="utf-8")
    assert "offsetof(OrpheusArena, front.eq_bank.bq)" in idc
    assert "ORPHEUS_MODULE_Front" in idc
    assert "sizeof(OrpheusMod_Front)" in idc

    md = (out / "memory_map.md").read_text(encoding="utf-8")
    assert "ORPHEUS_TUNE_FrontEqBankFc0" in md
    assert "sizeof(OrpheusMod_Front)" in md
    assert "0x50040000" in md  # MODULE_Front = 5<<28 | 4<<16


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_generated_nested_arena_runs() -> None:
    """嵌套子模块生成工程：模块嵌套 arena + id_map 编译运行，输出 wav。"""
    name = _new_name()
    with TestClient(create_app(ROOT)) as client:
        resp = client.post(
            "/api/projects", json={"name": name, "from_example": "dsp_model_reference"}
        )
        assert resp.status_code == 201, resp.text
        shutil.copy2(ROOT / "examples" / "test_input.wav", ROOT / "workspace" / name / "test_input.wav")
        run = client.post(f"/api/projects/{name}/run_generated")
        assert run.status_code == 200, run.text
        result = run.json()
        assert result["status"] == "ok", result["stderr"]
        assert result["outputs"] == ["outputs/model_out.wav"]
        gen = ROOT / "workspace" / name / "generated"
        assert (gen / "include" / "orpheus_ids.h").exists()
        assert (gen / "src" / "orpheus_id_map.c").exists()
        assert (gen / "memory_map.md").exists()
