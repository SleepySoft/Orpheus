"""蒸馏模型一键导入端点测试：YAML → 新工程；model_tree/presets 等顶层字段往返保留；可编译。"""

from __future__ import annotations

import uuid
import shutil
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]
_CREATED: list[str] = []


def _new_name() -> str:
    name = f"distill_{uuid.uuid4().hex[:8]}"
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


def test_distill_import_roundtrip_and_extra_fields() -> None:
    name = _new_name()
    text = (ROOT / "examples" / "dsp_model_reference.yaml").read_text(encoding="utf-8")
    with TestClient(create_app(ROOT)) as client:
        resp = client.post(f"/api/projects/{name}/distill", json={"yaml": text})
        assert resp.status_code == 200, resp.text
        doc = resp.json()["document"]
        # model_tree 注释随导入保留（顶层未知字段）
        assert doc["model_tree"]["name"].startswith("参考")

        # presets 等未知顶层字段经 put → 重载 → get 往返不丢
        doc["presets"] = [{"name": "p1", "created_at": "", "nodes": []}]
        assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
        got = client.get(f"/api/projects/{name}").json()
        assert got["presets"][0]["name"] == "p1"
        assert got["model_tree"]["name"].startswith("参考")

        # 三层嵌套编译通过
        compiled = client.post(f"/api/projects/{name}/compile")
        assert compiled.status_code == 200, compiled.text
        assert len(compiled.json()["execution_order"]) == 17


def test_distill_import_rejects_invalid_yaml() -> None:
    name = _new_name()
    with TestClient(create_app(ROOT)) as client:
        resp = client.post(f"/api/projects/{name}/distill", json={"yaml": "{{{{not yaml"})
        assert resp.status_code == 400
        resp = client.post(f"/api/projects/{name}/distill", json={"yaml": "foo: bar"})
        assert resp.status_code == 400
        assert "缺少 graph" in resp.json()["detail"]


def test_parse_flow_splits_blocks_and_respects_parens() -> None:
    """流程文本解析：顶层 -> 与 + 切块，括号内不拆，TIDn 前缀剥离。"""
    from orpheus_core.distill_topology import map_block, parse_flow

    blocks = parse_flow("A(1) -> B(2, 3ch->4ch) -> TID3: C + D -> E")
    assert [b["name"] for b in blocks] == ["A", "B", "C", "D", "E"]
    assert blocks[1]["params"] == "2, 3ch->4ch"
    assert map_block("MakeupGain") == "orpheus.builtin.gain"
    assert map_block("RFFT") == "orpheus.builtin.rfft"
    assert map_block("pooliir") == "orpheus.builtin.iir_bank"
    assert map_block("Coeffs1stStage") == "orpheus.builtin.placeholder"
    # 新增分析组件映射：相干矩阵 / 查表插值 / 功率谱
    assert map_block("FormCoherenceMatrixGXY") == "orpheus.builtin.coherence_matrix"
    assert map_block("SpeedBounds") == "orpheus.builtin.interp_lut"
    assert map_block("PSD") == "orpheus.builtin.psd"
    # 主链路由/选择 + FDP 频域系数施加：原占位 -> 已映射组件
    assert map_block("ApplyCoefficients") == "orpheus.builtin.matrix_mul"
    assert map_block("SelectSurroundDiscrete") == "orpheus.builtin.input_select"
    assert map_block("AudioOut") == "orpheus.builtin.output_router"
    assert map_block("PreqOut1") == "orpheus.builtin.output_router"


def test_distill_symphony_sas_topology_expansion() -> None:
    """symphony_sas_step0 的 model_tree.chains 在导入时展开为拓扑：
    主图含全部链子模块，未映射块用占位组件 id（UI 标红「组件缺失」），注释保留。

    symphony_sas_step0.yaml 本身已是可执行工程（20 节点），蒸馏端点只会在骨架图（<=3 节点）
    上展开 model_tree；因此测试时把 graph 替换为最小骨架，验证 model_tree 展开能力。"""
    import yaml

    name = _new_name()
    data = yaml.safe_load(
        (ROOT / "examples" / "symphony_sas_step0.yaml").read_text(encoding="utf-8")
    )
    # 蒸馏端点只在骨架图上展开 model_tree
    data["graph"] = {
        "nodes": [
            {
                "id": "sys_in",
                "component": "orpheus.builtin.embed_in",
                "params": {"channels": 22, "sample_rate": 48000},
                "position": {"x": 40, "y": 200},
            },
            {
                "id": "sys_out",
                "component": "orpheus.builtin.embed_out",
                "params": {"channels": 22, "sample_rate": 48000},
                "position": {"x": 560, "y": 200},
            },
        ],
        "connections": [{"from": "sys_in:out", "to": "sys_out:in"}],
    }
    text = yaml.safe_dump(data, sort_keys=False, allow_unicode=True)
    with TestClient(create_app(ROOT)) as client:
        resp = client.post(f"/api/projects/{name}/distill", json={"yaml": text})
        assert resp.status_code == 200, resp.text
        doc = resp.json()["document"]
        # 主音频链（TID0：9 链，含 FDP 内联）+ 4 个降速率分析抽头（TID2 inline 不生成抽头）
        assert len(doc["graph"]["nodes"]) == 23
        assert len(doc["subcomponents"]) == 13
        assert any(n["component"].startswith("sub:") for n in doc["graph"]["nodes"])
        downrates = {
            n["id"]: n["params"].get("factor")
            for n in doc["graph"]["nodes"]
            if n["component"] == "orpheus.builtin.downrate"
        }
        assert downrates == {
            "downrate_1": 2,
            "downrate_3": 64,
            "downrate_4": 256,
            "downrate_5": 768,
        }
        # part2_fdp（TID2，mode=inline）在主音频链里，不是 tap 抽头
        main_chain = [n["id"] for n in doc["graph"]["nodes"]]
        assert "part2_fdp" in main_chain
        assert "tap_1" in main_chain and "tap_2" not in main_chain and "tap_5" in main_chain
        comps = [n["component"] for s in doc["subcomponents"] for n in s["graph"]["nodes"]]
        assert "orpheus.builtin.placeholder" in comps
        assert any(c.startswith("orpheus.builtin.") and c != "orpheus.builtin.placeholder" for c in comps)
        # 分析组件已接入（原为占位）：相干矩阵 / 查表插值 / 功率谱
        assert "orpheus.builtin.coherence_matrix" in comps
        assert "orpheus.builtin.interp_lut" in comps
        assert "orpheus.builtin.psd" in comps
        # model_tree 注释随展开保留，且经重载往返不丢
        got = client.get(f"/api/projects/{name}").json()
        assert len(got["subcomponents"]) == 13
        assert got["model_tree"]["name"].startswith("Symphony")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_distilled_model_runs_end_to_end() -> None:
    """蒸馏导入的复杂嵌套模型离线运行：输出 WAV，且嵌套层探针以 flatId 上报。"""
    name = _new_name()
    text = (ROOT / "examples" / "dsp_model_reference.yaml").read_text(encoding="utf-8")
    with TestClient(create_app(ROOT)) as client:
        assert client.post(f"/api/projects/{name}/distill", json={"yaml": text}).status_code == 200
        pdir = ROOT / "workspace" / name
        shutil.copy2(ROOT / "examples" / "test_input.wav", pdir / "test_input.wav")
        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        by = {(p["node"], p["param"]): p["value"] for p in result["probes"]}
        # 三层嵌套内的探针：front__mon.rms、post__fir.taps、out_mon.rms
        assert ("front__mon", "rms") in by
        assert ("post__fir", "taps") in by
        assert ("out_mon", "rms") in by
        assert (pdir / "outputs" / "model_out.wav").exists()
