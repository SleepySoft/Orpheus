"""API-level tests for the Orpheus HTTP server."""

from __future__ import annotations

import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]  # repository root


@pytest.fixture()
def client():
    with TestClient(create_app(ROOT)) as c:
        yield c


@pytest.fixture()
def project(client):
    """Create a uniquely-named project and clean it up afterwards."""
    name = f"test_{uuid.uuid4().hex[:8]}"
    resp = client.post("/api/projects", json={"name": name})
    assert resp.status_code == 201, resp.text
    yield name
    client.delete(f"/api/projects/{name}")


def test_health_and_components(client):
    assert client.get("/api/health").json()["status"] == "ok"
    comps = client.get("/api/components").json()
    assert len(comps) >= 14
    by_id = {c["id"]: c for c in comps}
    gain = by_id["orpheus.builtin.gain"]
    assert {p["id"] for p in gain["ports"]} == {"in", "out"}
    assert any(p["id"] == "gain_db" for p in gain["parameters"])


def test_project_lifecycle(client, project):
    # listed
    names = [p["name"] for p in client.get("/api/projects").json()]
    assert project in names

    # read back and modify
    doc = client.get(f"/api/projects/{project}").json()
    assert doc["graph"]["nodes"] == []
    doc["metadata"]["description"] = "updated"
    resp = client.put(f"/api/projects/{project}", json=doc)
    assert resp.status_code == 200, resp.text
    assert client.get(f"/api/projects/{project}").json()["metadata"]["description"] == "updated"

    # invalid document rejected
    resp = client.put(f"/api/projects/{project}", json={"foo": 1})
    assert resp.status_code == 400

    # delete -> 404
    assert client.delete(f"/api/projects/{project}").status_code == 200
    assert client.get(f"/api/projects/{project}").status_code == 404


def test_duplicate_project_rejected(client, project):
    resp = client.post("/api/projects", json={"name": project})
    assert resp.status_code == 409


def test_import_example_and_compile(client):
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "wav_gain_biquad"})
        assert resp.status_code == 201, resp.text
        doc = resp.json()["document"]
        assert len(doc["graph"]["nodes"]) == 4

        # absolute example paths rewritten to project-relative paths
        params = {n["id"]: n["params"] for n in doc["graph"]["nodes"]}
        assert params["wav_in"]["file_path"] == "test_input.wav"
        assert params["wav_out"]["file_path"] == "outputs/test_output.wav"
        pdir = ROOT / "workspace" / name
        assert (pdir / "test_input.wav").exists()

        resp = client.post(f"/api/projects/{name}/compile")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["nodes"] == 4
        assert len(result["execution_order"]) == 4
        assert (pdir / "project.plan.json").exists()
    finally:
        client.delete(f"/api/projects/{name}")


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_run_example_end_to_end(client):
    name = f"test_{uuid.uuid4().hex[:8]}"
    try:
        resp = client.post("/api/projects", json={"name": name, "from_example": "wav_gain_biquad"})
        assert resp.status_code == 201, resp.text

        resp = client.post(f"/api/projects/{name}/run")
        assert resp.status_code == 200, resp.text
        result = resp.json()
        assert result["status"] == "ok", result["stderr"]
        assert "outputs/test_output.wav" in result["outputs"]

        out = ROOT / "workspace" / name / "outputs" / "test_output.wav"
        assert out.exists() and out.stat().st_size > 44

        # file endpoint serves the output wav
        resp = client.get(f"/api/projects/{name}/files/outputs/test_output.wav")
        assert resp.status_code == 200
        assert len(resp.content) == out.stat().st_size

        # path traversal blocked
        assert client.get(f"/api/projects/{name}/files/../../README.md").status_code in (400, 404)

        # zip download
        resp = client.get(f"/api/projects/{name}/download")
        assert resp.status_code == 200
        assert resp.content[:2] == b"PK"
    finally:
        client.delete(f"/api/projects/{name}")
