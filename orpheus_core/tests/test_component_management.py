"""自定义组件管理测试：user_owned 删除/提升为公共库。"""

from __future__ import annotations

import shutil
from pathlib import Path

from fastapi.testclient import TestClient

from orpheus_core.scaffold import scaffold_custom_component
from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = "zz_test_comp"


def test_component_delete_and_promote() -> None:
    # 场景一：公共库不可删；自定义组件提升后也不可直接删除
    d = scaffold_custom_component(ROOT, SCRATCH)
    try:
        with TestClient(create_app(ROOT)) as client:
            assert client.delete("/api/components/orpheus.builtin.gain").status_code == 400
            r = client.post(f"/api/components/orpheus.builtin.{SCRATCH}/promote")
            assert r.status_code == 200, r.text
            assert client.delete(f"/api/components/orpheus.builtin.{SCRATCH}").status_code == 400
    finally:
        if d.exists():
            shutil.rmtree(d)

    # 场景二：用户自定义组件（user_owned）可删除，源码目录被移除
    d2 = scaffold_custom_component(ROOT, SCRATCH)
    try:
        with TestClient(create_app(ROOT)) as client:
            r = client.delete(f"/api/components/orpheus.builtin.{SCRATCH}")
            assert r.status_code == 200, r.text
            assert r.json()["deleted"] == f"orpheus.builtin.{SCRATCH}"
            assert not d2.exists()
    finally:
        if d2.exists():
            shutil.rmtree(d2)
