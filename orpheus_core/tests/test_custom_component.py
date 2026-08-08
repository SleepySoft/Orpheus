"""自定义组件壳（脚手架）测试：文件隔离 + CUSTOM 消息走组件 hook。"""

from __future__ import annotations

import json
import struct
import subprocess
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.scaffold import scaffold_custom_component
from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]
_CREATED: list[str] = []


def _make_msg(route: int, call_id: int, msg_type: int = 0, payload: bytes = b"") -> bytes:
    words = (len(payload) + 3) // 4
    bits = (msg_type << 30) | (call_id << 10) | words
    return struct.pack("<II", route, bits) + payload.ljust(words * 4, b"\x00")


def _parse(frame: bytes) -> dict:
    route, bits = struct.unpack("<II", frame[:8])
    return {
        "route": route,
        "type": (bits >> 30) & 0x3,
        "flags": (bits >> 26) & 0xF,
        "call_id": (bits >> 10) & 0xFFFF,
        "payload": frame[8: 8 + (bits & 0x3FF) * 4],
    }


def _new_name() -> str:
    name = f"cc_{uuid.uuid4().hex[:8]}"
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


def test_scaffold_generates_isolated_files(tmp_path: Path) -> None:
    d = scaffold_custom_component(tmp_path, "widget")
    assert (d / "component.yaml").exists()
    assert (d / "src" / "widget.c").exists()
    assert (d / "include" / "orpheus_widget.h").exists()
    assert (d / "user" / "widget_user.c").exists()
    assert (d / "user" / "widget_user.h").exists()
    src = (d / "src" / "widget.c").read_text(encoding="utf-8")
    assert ".hook = widget_hook" in src
    assert 'ORPHEUS_ENTRY_NAME' in src
    assert '#include "widget_user.h"' in src
    yaml = (d / "component.yaml").read_text(encoding="utf-8")
    assert "custom_handles" in yaml and "reply: true" in yaml
    user = (d / "user" / "widget_user.c").read_text(encoding="utf-8")
    assert "生成器永不覆盖" in user
    with pytest.raises(FileExistsError):
        scaffold_custom_component(tmp_path, "widget")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime / my_effect not built",
)
def test_custom_component_custom_message() -> None:
    from orpheus_core.registry import Registry

    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    if reg.get("orpheus.builtin.my_effect") is None:
        pytest.skip("演示组件 my_effect 未安装（可在组件库中重新创建）")

    name = _new_name()
    with TestClient(create_app(ROOT)) as client:
        assert client.post("/api/projects", json={"name": name}).status_code == 201
        doc = client.get(f"/api/projects/{name}").json()
        doc["graph"] = {
            "nodes": [
                {"id": "sig", "component": "orpheus.builtin.signal_gen",
                 "params": {"sample_rate": 48000, "channels": 1}, "position": {"x": 0, "y": 0}},
                {"id": "fx", "component": "orpheus.builtin.my_effect",
                 "params": {"channels": 1}, "position": {"x": 200, "y": 0}},
                {"id": "out", "component": "orpheus.builtin.wav_out",
                 "params": {"file_path": "outputs/out.wav", "channels": 1, "sample_rate": 48000},
                 "position": {"x": 400, "y": 0}},
            ],
            "connections": [
                {"from": "sig:out", "to": "fx:in"},
                {"from": "fx:out", "to": "out:in"},
            ],
        }
        assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
        assert client.post(f"/api/projects/{name}/compile").status_code == 200
    cwd = ROOT / "workspace" / name
    plan = json.loads((cwd / "project.plan.json").read_text(encoding="utf-8"))
    custom = [e for e in plan["id_map"] if e["kind"] == "CUSTOM"]
    assert {e["key"] for e in custom} >= {"reset", "snapshot"}
    snap = next(e for e in custom if e["key"] == "snapshot")
    reset = next(e for e in custom if e["key"] == "reset")

    exe = ROOT / "build" / "orpheus_runtime.exe"
    comps = ROOT / "build" / "components"

    def run(*args: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(exe), str(cwd / "project.plan.json"), str(comps), *args],
            capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
        )

    # CUSTOM 消息走组件 hook（user_handle 回显）：snapshot 是 response 语义
    r = run("--msg", _make_msg(snap["id"], 0x41, payload=b"ECHO").hex())
    assert r.returncode == 0 and r.stdout.startswith("MSGRSP "), r.stderr
    resp = _parse(bytes.fromhex(r.stdout.strip()[7:]))
    assert resp["flags"] == 0 and resp["payload"].rstrip(b"\x00") == b"ECHO"
    # reset 是 notification：无返回
    r = run("--msg", _make_msg(reset["id"], 0x42, msg_type=2, payload=b"GO").hex())
    assert r.stdout.strip() == "MSGNONE"

    # 离线运行验证默认直通 process
    r = run()
    assert r.returncode == 0 and "Processed" in r.stdout, r.stderr
    assert (cwd / "outputs" / "out.wav").exists()
