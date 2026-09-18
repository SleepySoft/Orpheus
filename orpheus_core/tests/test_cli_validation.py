from __future__ import annotations

import json
from pathlib import Path

import yaml
from click.testing import CliRunner

from orpheus_core.cli import cli

ROOT = Path(__file__).resolve().parents[2]


def test_validate_reports_success_and_failure(tmp_path: Path) -> None:
    valid = tmp_path / "valid.yaml"
    valid.write_text(yaml.safe_dump({
        "version": "0.1.0",
        "metadata": {"name": "valid"},
        "graph": {"nodes": [], "connections": []},
    }), encoding="utf-8")

    invalid = tmp_path / "invalid.yaml"
    invalid.write_text(yaml.safe_dump({
        "version": 123,
        "graph": {
            "nodes": [
                {
                    "id": "broken",
                    "component": "orpheus.builtin.does_not_exist",
                    "task": "missing",
                    "params": {"gain_db": "not-a-number"},
                }
            ],
            "connections": [],
        },
    }), encoding="utf-8")

    runner = CliRunner()
    result = runner.invoke(cli, ["--project-root", str(ROOT), "validate", str(invalid), "--json"])
    assert result.exit_code == 1
    payload = json.loads(result.output)
    assert payload["summary"]["checked"] == 1
    assert payload["summary"]["invalid"] == 1
    report = payload["projects"][0]
    assert report["valid"] is False
    assert report["stages"]["schema"] == "failed"
    assert report["stages"]["structure"] == "failed"
    assert report["stages"]["references"] == "failed"
    assert report["stages"]["compile"] == "skipped"
    messages = " ".join(issue["message"] for issue in report["issues"])
    assert "组件不存在" in messages
    assert "引用不存在的 Task" in messages

    result = runner.invoke(cli, ["--project-root", str(ROOT), "validate", str(valid)])
    assert result.exit_code == 0, result.output
    assert f"PASS {valid}" in result.output
    assert "1/1 valid" in result.output


def test_validate_reports_parameter_without_compile(tmp_path: Path) -> None:
    project = tmp_path / "parameter.yaml"
    project.write_text(yaml.safe_dump({
        "version": "0.1.0",
        "metadata": {"name": "parameter"},
        "graph": {
            "nodes": [{
                "id": "gain",
                "component": "orpheus.builtin.gain",
                "params": {"gain_db": -300.0, "channels": 1},
            }],
            "connections": [],
        },
    }), encoding="utf-8")

    runner = CliRunner()
    result = runner.invoke(
        cli,
        ["--project-root", str(ROOT), "validate", str(project), "--no-compile", "--json"],
    )
    assert result.exit_code == 1, result.output
    payload = json.loads(result.output)
    report = payload["projects"][0]
    assert report["stages"]["structure"] == "passed"
    assert report["stages"]["references"] == "passed"
    assert report["stages"]["compile"] == "skipped"
    assert any(issue["stage"] == "component" for issue in report["issues"])
