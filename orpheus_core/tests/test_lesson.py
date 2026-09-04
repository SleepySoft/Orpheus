"""Teaching package structural checks and API integration."""

from __future__ import annotations

import uuid
from pathlib import Path

from fastapi.testclient import TestClient

from orpheus_core.compiler import GraphCompiler
from orpheus_core.lesson import evaluate_lesson
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]


def make_lesson_project() -> Project:
    project = Project(metadata={"name": "lesson"})
    project.graph = Graph(
        nodes={
            "src": Node(id="src", component="orpheus.builtin.signal_gen", params={"channels": 1}),
            "gain": Node(id="gain", component="orpheus.builtin.gain", params={"channels": 1, "gain_db": -6.0}),
        },
        connections=[Connection(PortRef.parse("src:out"), PortRef.parse("gain:in"))],
    )
    project.extra["lesson"] = {
        "title": "增益实验",
        "steps": [{"title": "设置增益", "body": "把 gain 调到 -6 dB"}],
        "checks": [
            {"id": "compile", "type": "compile_valid"},
            {"id": "component", "type": "node_component", "node": "gain", "component": "orpheus.builtin.gain"},
            {"id": "value", "type": "parameter_equals", "node": "gain", "param": "gain_db", "value": -6.0},
            {"id": "edge", "type": "connection_exists", "from": "src:out", "to": "gain:in"},
        ],
    }
    return project


def test_evaluate_lesson_all_rules() -> None:
    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = make_lesson_project()
    plan = GraphCompiler(registry).compile(project)
    result = evaluate_lesson(project, project, plan)
    assert result["passed"] is True
    assert result["passed_count"] == result["total"] == 4


def test_lesson_check_api() -> None:
    name = f"lesson_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            document = {
                "version": "0.1.0",
                "metadata": {"name": name},
                "graph": {
                    "nodes": [{"id": "g", "component": "orpheus.builtin.gain", "params": {"channels": 1}}],
                    "connections": [],
                },
                "lesson": {
                    "title": "检查",
                    "steps": [],
                    "checks": [{
                        "id": "gain", "type": "node_component", "node": "g",
                        "component": "orpheus.builtin.gain",
                    }],
                },
            }
            assert client.put(f"/api/projects/{name}", json=document).status_code == 200
            response = client.post(f"/api/projects/{name}/lesson/check")
            assert response.status_code == 200
            assert response.json()["passed"] is True
        finally:
            client.delete(f"/api/projects/{name}")


def test_malformed_lesson_checks_return_failures() -> None:
    project = Project()
    project.extra["lesson"] = {
        "checks": [
            "bad",
            {"id": "missing", "type": "parameter_equals", "param": "gain"},
            {"id": "tolerance", "type": "parameter_equals", "node": "g", "param": "gain", "tolerance": "bad"},
        ]
    }
    result = evaluate_lesson(project, project, None, "compile unavailable")
    assert result["passed"] is False
    assert result["total"] == 3
    assert all(not item["passed"] for item in result["results"])
