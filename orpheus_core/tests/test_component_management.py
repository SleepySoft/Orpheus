"""自定义组件管理测试：user_owned 删除/提升为公共库。"""

from __future__ import annotations

import shutil
import uuid
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


def test_delete_blocked_while_referenced() -> None:
    """仍被工程引用的自定义组件禁止删除；移除引用后可删除。"""
    d = scaffold_custom_component(ROOT, SCRATCH)
    try:
        with TestClient(create_app(ROOT)) as client:
            name = f"ref_{uuid.uuid4().hex[:8]}"
            try:
                assert client.post("/api/projects", json={"name": name}).status_code == 201
                doc = client.get(f"/api/projects/{name}").json()
                doc["graph"] = {
                    "nodes": [
                        {
                            "id": "fx",
                            "component": f"orpheus.builtin.{SCRATCH}",
                            "params": {"channels": 2},
                            "position": {"x": 0, "y": 0},
                        }
                    ],
                    "connections": [],
                }
                assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
                r = client.delete(f"/api/components/orpheus.builtin.{SCRATCH}")
                assert r.status_code == 400 and "仍被工程" in r.json()["detail"]
                assert d.exists()
                # 移除引用后可删除
                doc["graph"]["nodes"] = []
                assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
                assert client.delete(f"/api/components/orpheus.builtin.{SCRATCH}").status_code == 200
                assert not d.exists()
            finally:
                client.delete(f"/api/projects/{name}")
    finally:
        if d.exists():
            shutil.rmtree(d)
