"""通用组件库的命名、说明及功能分类契约。"""
from pathlib import Path
import re

import pytest
import yaml

from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]
MANIFESTS = sorted((ROOT / "components" / "orpheus" / "builtin").glob("*/component.yaml"))
CATEGORIES = {
    "输入与输出", "驱动与缓冲", "路由与混音", "增益与动态", "滤波与均衡",
    "频域与分析", "自适应与降噪", "数学与控制", "平台与通信",
}


@pytest.mark.parametrize("path", MANIFESTS, ids=lambda path: path.parent.name)
def test_common_component_has_functional_metadata(path):
    manifest = yaml.safe_load(path.read_text(encoding="utf-8"))
    assert re.search(r"[\u4e00-\u9fff]", manifest["name"])
    assert manifest["description"].strip()
    assert manifest["category"] in CATEGORIES
    assert not re.search(r"头枕|SleepingBeauty|3D|双二阶", manifest["name"])


def test_common_library_names_are_unique_and_specialized_protocols_are_absent():
    manifests = [yaml.safe_load(path.read_text(encoding="utf-8")) for path in MANIFESTS]
    assert len({item["name"] for item in manifests}) == len(manifests)
    assert {item["category"] for item in manifests} == CATEGORIES
    registry = Registry([ROOT / "components"])
    registry.scan()
    assert registry.get("orpheus.builtin.call_snapshot") is None
    assert registry.get("orpheus.builtin.conditional_call") is None
