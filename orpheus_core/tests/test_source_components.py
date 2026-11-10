"""源码组件创建只使用落盘配置和独立 CLI，且遵守工程私有边界。"""
import json
import os
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.server.app import COMPONENT_SCRIPTS, create_app

ROOT = Path(__file__).resolve().parents[2]
CONFIGURATION = """id: project.custom.effect
name: 自建音效
version: 0.1.0
interfaces:
  input: {type: audio, direction: input, channels: 1, process: true}
  output: {type: audio, direction: output, channels: 'input:channels'}
  gain: {type: float, default: 1.0, update_policy: immediate}
"""


@pytest.fixture
def source_client():
    root = ROOT / "workspace" / f"test_source_{uuid.uuid4().hex}"
    root.mkdir(parents=True)
    try:
        with TestClient(create_app(root)) as client:
            token = client.post("/api/editor/acquire", json={}).json()["token"]
            client.headers["X-Orpheus-Edit-Token"] = token
            assert client.post("/api/projects", json={"name": "demo"}).status_code == 201
            yield client, root / "workspace" / "demo"
    finally:
        shutil.rmtree(root)


URL = "/api/projects/demo/source-components/my_effect"


def save(client, configuration=CONFIGURATION, revision=None):
    return client.put(URL, json={"configuration": configuration, "expected_sha256": revision})


def run(client, revision, operation="generate"):
    return client.post(f"{URL}/{operation}", json={"expected_sha256": revision})


def test_disk_first_cli_parity_and_private_registration(source_client, monkeypatch):
    client, directory = source_client
    config = directory / "components" / "my_effect" / "component.yaml"
    calls = []
    real_run = subprocess.run

    def observed(argv, **kwargs):
        assert config.read_text(encoding="utf-8") == CONFIGURATION
        assert argv[0] == sys.executable
        assert Path(argv[1]).is_absolute()
        assert Path(argv[1]).parent == COMPONENT_SCRIPTS
        assert argv[2:] == [str(config), "--format", "json"]
        result = real_run(argv, **kwargs)
        calls.append((Path(argv[1]).name, json.loads(result.stdout)))
        return result

    monkeypatch.setattr("orpheus_core.server.app.subprocess.run", observed)
    saved = save(client).json()
    assert saved["saved"] and not saved["registered"]
    assert calls == []
    response = run(client, saved["sha256"])
    report = response.json()
    assert report["ok"], report
    assert report["registered"]
    assert [name for name, _ in calls] == ["validate_component.py", "generate_component.py"]
    for key in ("ok", "files", "diagnostics"):
        assert report[key] == calls[-1][1][key]
    assert report["imports"] == ["components/my_effect/component.yaml"]
    assert client.get("/api/projects/demo").json()["imports"] == report["imports"]
    assert any(item["id"] == "project.custom.effect" and item["project_local"]
               for item in client.get("/api/projects/demo/components").json())
    assert not client.get("/api/components").json()
    # CLI 在另一目录读取同一份配置，生成区逐字节相同（不比较仅首次创建文件列表）。
    cli_directory = directory / "cli_comparison"
    cli_directory.mkdir()
    cli_config = cli_directory / "component.yaml"
    cli_config.write_bytes(config.read_bytes())
    completed = real_run([sys.executable, str(COMPONENT_SCRIPTS / "generate_component.py"),
                          str(cli_config), "--format", "json"],
                         capture_output=True, text=True, encoding="utf-8")
    assert completed.returncode == 0, completed.stdout
    for generated in (config.parent / "generated").iterdir():
        assert generated.read_bytes() == (cli_directory / "generated" / generated.name).read_bytes()


def test_validation_failure_keeps_configuration_and_can_revise(source_client):
    client, directory = source_client
    saved = save(client, "interfaces: [").json()
    response = run(client, saved["sha256"]).json()
    assert not response["ok"] and not response["registered"]
    assert response["phase"] == "validation"
    assert response["diagnostics"]
    cli = subprocess.run(
        [sys.executable, str(COMPONENT_SCRIPTS / "validate_component.py"),
         str(directory / "components" / "my_effect" / "component.yaml"), "--format", "json"],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert cli.returncode == 1
    assert json.loads(cli.stdout)["diagnostics"] == response["diagnostics"]
    assert client.get(URL).json()["configuration"] == "interfaces: ["
    assert "imports" not in client.get("/api/projects/demo").json()
    assert not (directory / "components" / "my_effect" / "generated").exists()
    revised = save(client, revision=saved["sha256"]).json()
    assert run(client, revised["sha256"]).json()["registered"]


def test_generation_failure_preserves_algorithm_and_generated_files(source_client):
    client, directory = source_client
    saved = save(client).json()
    assert run(client, saved["sha256"]).json()["registered"]
    package = directory / "components" / "my_effect"
    algorithm = package / "user" / "algorithm.c"
    algorithm.write_text("/* 自有算法，不得覆盖 */\n", encoding="utf-8")
    cmake = package / "CMakeLists.txt"
    cmake.write_text("# 自有构建选项\n", encoding="utf-8")
    repeated = run(client, saved["sha256"]).json()
    assert repeated["ok"], repeated
    assert algorithm.read_text(encoding="utf-8") == "/* 自有算法，不得覆盖 */\n"
    assert cmake.read_text(encoding="utf-8") == "# 自有构建选项\n"
    shell = package / "generated" / "component_shell.c"
    shell.write_text("/* 手工修改 */\n", encoding="utf-8")
    snapshot = {p: p.read_bytes() for p in package.rglob("*") if p.is_file()}
    report = run(client, saved["sha256"]).json()
    assert not report["ok"] and not report["registered"] and report["phase"] == "generation"
    assert report["diagnostics"][0]["code"] == "ownership_error"
    assert all(p.read_bytes() == content for p, content in snapshot.items())


def test_path_revision_lease_and_missing_save_boundaries(source_client):
    client, directory = source_client
    assert run(client, "none").status_code == 409
    for package in ("Bad", "con", "aux", "COM1", "a.b", "a%5Cb", "a%3Ab", "%2E%2E"):
        response = client.put(URL.replace("my_effect", package), json={"configuration": CONFIGURATION})
        assert response.status_code in {400, 404}, (package, response.text)
    package = directory / "components" / "my_effect"
    package.mkdir(parents=True)
    algorithm = package / "algorithm.c"
    algorithm.write_text("/* 原有组件 */", encoding="utf-8")
    assert save(client).status_code == 409
    algorithm.unlink()
    saved = save(client).json()
    assert save(client).status_code == 409
    assert run(client, "wrong").status_code == 409
    client.headers.pop("X-Orpheus-Edit-Token")
    assert save(client, revision=saved["sha256"]).status_code == 409
    assert run(client, saved["sha256"]).status_code == 409


def test_script_failure_does_not_claim_registration(source_client, monkeypatch):
    client, directory = source_client
    saved = save(client).json()

    def failed(*args, **kwargs):
        raise subprocess.TimeoutExpired(args[0], 60)

    monkeypatch.setattr("orpheus_core.server.app.subprocess.run", failed)
    report = run(client, saved["sha256"]).json()
    assert report["saved"] and not report["ok"] and not report["registered"]
    assert (directory / "components" / "my_effect" / "component.yaml").is_file()
    assert "imports" not in client.get("/api/projects/demo").json()


def test_registration_failure_does_not_persist_import(source_client, monkeypatch):
    client, directory = source_client
    saved = save(client).json()
    from orpheus_core.server.manager import ProjectManager, ProjectValidationError

    def rejected(*args, **kwargs):
        raise ProjectValidationError("工程保存被拒绝")

    monkeypatch.setattr(ProjectManager, "put", rejected)
    report = run(client, saved["sha256"]).json()
    assert not report["ok"] and not report["registered"] and report["phase"] == "registration"
    assert "工程保存被拒绝" in report["detail"]
    assert "imports" not in client.get("/api/projects/demo").json()
    assert (directory / "components" / "my_effect" / "generated").is_dir()


def test_directory_links_cannot_escape_private_paths(source_client):
    client, directory = source_client
    outside = directory / "outside"
    outside.mkdir()

    def link_directory(link):
        if sys.platform == "win32":
            result = subprocess.run(
                ["cmd", "/c", "mklink", "/J", str(link), str(outside)], capture_output=True,
            )
            assert result.returncode == 0, result.stderr
        else:
            link.symlink_to(outside, target_is_directory=True)

    components = directory / "components"
    link_directory(components)
    try:
        assert save(client).status_code == 400
        assert not list(outside.iterdir())
    finally:
        components.rmdir() if sys.platform == "win32" else components.unlink()
    saved = save(client).json()
    user = directory / "components" / "my_effect" / "user"
    link_directory(user)
    try:
        assert run(client, saved["sha256"]).status_code == 400
        assert not list(outside.iterdir())
    finally:
        user.rmdir() if sys.platform == "win32" else user.unlink()


def test_revision_save_is_atomic_and_does_not_modify_hardlinked_file(source_client):
    client, directory = source_client
    saved = save(client).json()
    configuration = directory / "components" / "my_effect" / "component.yaml"
    shared = directory / "shared_configuration.yaml"
    os.link(configuration, shared)
    revised = save(client, CONFIGURATION + "# 修订\n", saved["sha256"])
    assert revised.status_code == 200
    assert shared.read_text(encoding="utf-8") == CONFIGURATION
    assert configuration.read_text(encoding="utf-8").endswith("# 修订\n")
    assert not list(configuration.parent.glob(".component_configuration_*"))


def test_trigger_template_uses_cli_without_pcm_or_virtual_drive_ports(source_client):
    client, directory = source_client
    configuration = """id: project.custom.trigger
name: 纯触发组件
version: 0.1.0
category: 工程/自建组件
interfaces:
  tick:
    type: trigger
    direction: input
    process: true
  next:
    type: trigger
    direction: output
    drive_port_no: 1
"""
    saved = save(client, configuration).json()
    report = run(client, saved["sha256"]).json()
    assert report["ok"] and report["registered"], report
    ports = report["component"]["ports"]
    assert [port["id"] for port in ports] == ["tick", "next"]
    assert all(port["type"] == "trigger" for port in ports)
    assert ports[1]["drive_port_no"] == 1
    api = (directory / "components" / "my_effect" / "generated" / "component_api.h").read_text(
        encoding="utf-8")
    assert "OutputTrigger" in api and "InputTrigger" in api
    assert "project_custom_trigger_InputTrigger tick;" in api
    assert "project_custom_trigger_OutputTrigger next;" in api
    assert "InputF32 tick;" not in api and "OutputF32 next;" not in api
