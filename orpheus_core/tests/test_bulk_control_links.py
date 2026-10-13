"""BULK control_connections 动态 Runtime 两相快照与块边界提交测试。"""

from __future__ import annotations

import subprocess
import json
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app
from test_generated_double_bank import _build, _run

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_bulk_control_link_reaches_target_on_next_block() -> None:
    name = f"bulk_ctl_{uuid.uuid4().hex[:8]}"
    try:
        with TestClient(create_app(ROOT)) as client:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            document = client.get(f"/api/projects/{name}").json()
            document["graph"] = {
                "nodes": [
                    {
                        "id": "source",
                        "component": "orpheus.builtin.bulk_relay",
                        "params": {"count": 4, "initial_values": "1.25,-2.5,3.75,4.5"},
                    },
                    {
                        "id": "target",
                        "component": "orpheus.builtin.bulk_relay",
                        "params": {"count": 4, "initial_values": "0,0,0,0"},
                    },
                ],
                "connections": [],
            }
            document["control_connections"] = [
                {"from": "source:values", "to": "target:values"}
            ]
            assert client.put(f"/api/projects/{name}", json=document).status_code == 200
            compiled = client.post(f"/api/projects/{name}/compile")
            assert compiled.status_code == 200, compiled.text
            link = compiled.json()["control_links"][0]
            assert link["transport"] == "bulk"
            assert link["count"] == 4
            assert link["src_slot"] == "values_storage"
            assert link["dst_slot"] == "values_storage"

        project_dir = ROOT / "workspace" / name
        persisted_plan = json.loads(
            (project_dir / "project.plan.json").read_text(encoding="utf-8")
        )
        persisted_link = persisted_plan["control_links"][0]
        assert persisted_link["transport"] == "bulk"
        assert persisted_link["src_slot"] == "values_storage"
        assert persisted_link["dst_slot"] == "values_storage"
        source_bulk_id = next(
            entry["id"] for entry in persisted_plan["id_map"]
            if entry["node"] == "source" and entry["key"] == "values_storage"
        )
        source_process = subprocess.run(
            [
                str(ROOT / "build" / "orpheus_runtime.exe"),
                str(project_dir / "project.plan.json"),
                str(ROOT / "build" / "components"),
                "--getbulk", "source", "values_storage",
            ],
            cwd=project_dir,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        assert source_process.returncode == 0, source_process.stderr
        source_line = next(
            line for line in source_process.stdout.splitlines() if line.startswith("BULKVALUE ")
        )
        assert [float(value) for value in source_line.split()[2:6]] == [1.25, -2.5, 3.75, 4.5]

        process = subprocess.run(
            [
                str(ROOT / "build" / "orpheus_runtime.exe"),
                str(project_dir / "project.plan.json"),
                str(ROOT / "build" / "components"),
                "--rwb", str(source_bulk_id), "4", "1.25", "-2.5", "3.75", "4.5",
                "--run", "2",
                "--getbulk", "target", "values_storage",
            ],
            cwd=project_dir,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        assert process.returncode == 0, process.stderr
        lines = [line for line in process.stdout.splitlines() if line.startswith("BULKVALUE ")]
        assert lines, process.stdout + process.stderr
        line = lines[0]
        assert [float(value) for value in line.split()[2:6]] == [1.25, -2.5, 3.75, 4.5], (
            process.stderr + process.stdout
        )
    finally:
        with TestClient(create_app(ROOT)) as client:
            client.delete(f"/api/projects/{name}")


def test_generated_bulk_control_link_matches_dynamic_path() -> None:
    name = f"bulk_ctl_gen_{uuid.uuid4().hex[:8]}"
    try:
        with TestClient(create_app(ROOT)) as client:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            document = client.get(f"/api/projects/{name}").json()
            document["graph"] = {
                "nodes": [
                    {"id": "source", "component": "orpheus.builtin.bulk_relay",
                     "params": {"count": 4, "initial_values": "1.25,-2.5,3.75,4.5"}},
                    {"id": "target", "component": "orpheus.builtin.bulk_relay",
                     "params": {"count": 4, "initial_values": "0,0,0,0"}},
                ],
                "connections": [],
            }
            document["control_connections"] = [
                {"from": "source:values", "to": "target:values"}
            ]
            assert client.put(f"/api/projects/{name}", json=document).status_code == 200
            generated = client.post(f"/api/projects/{name}/generate")
            assert generated.status_code == 200, generated.text

        project_dir = ROOT / "workspace" / name
        graph_source = (project_dir / "generated" / "src" / "orpheus_graph.c").read_text(
            encoding="utf-8"
        )
        control_source = (project_dir / "generated" / "src" / "orpheus_control.c").read_text(
            encoding="utf-8"
        )
        assert "g_iface_target->register_slots" in graph_source
        assert 'strcmp(node, "target")' in control_source
        assert 'strcmp(key, "values_storage")' in control_source
        executable = _build(project_dir / "generated", project_dir)
        process = _run(
            executable, project_dir, "0", "--run", "2",
            "--read-bulk", "target", "values_storage",
        )
        assert process.returncode == 0, process.stderr
        lines = [line for line in process.stdout.splitlines() if line.startswith("BULKVALUE ")]
        assert lines, process.stdout + process.stderr
        line = lines[0]
        assert [float(value) for value in line.split()[3:7]] == [1.25, -2.5, 3.75, 4.5]
    finally:
        with TestClient(create_app(ROOT)) as client:
            client.delete(f"/api/projects/{name}")