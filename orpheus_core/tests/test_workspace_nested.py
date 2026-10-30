"""嵌套 workspace 的工程发现、路径隔离与 HTTP 工作流。"""

from pathlib import Path
import os
import shutil
from urllib.parse import quote

from fastapi.testclient import TestClient
import pytest
import yaml

from orpheus_core.server.app import create_app
from orpheus_core.server.manager import ProjectManager, ProjectValidationError

ROOT = Path(__file__).resolve().parents[2]


def write_project(directory: Path, label: str = "demo") -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "project.yaml").write_text(
        f"version: 0.1.0\nmetadata: {{name: {label}}}\n"
        "graph: {nodes: [], connections: []}\n",
        encoding="utf-8",
    )


def project_url(name: str) -> str:
    return "/api/projects/" + quote(name.replace("/", ":"), safe="")


def test_discover_cloned_repositories_and_nested_projects(tmp_path):
    manager = ProjectManager(tmp_path)
    names = ["flat", "repo-a/examples/demo", "repo-b/examples/demo",
             "parent", "parent/child", "中文仓库/模型 v1.2"]
    for name in names:
        write_project(manager.workspace / name)
    (manager.workspace / "group-only").mkdir()
    write_project(manager.workspace / "repo-a/.git/hidden")
    write_project(manager.workspace / "repo-a/node_modules/dependency")
    write_project(manager.workspace)
    (manager.workspace / "not-a-project").mkdir()
    (manager.workspace / "not-a-project/project.yaml").mkdir()

    assert [item["name"] for item in manager.list_projects()] == sorted(names)
    rec = manager.get("repo-a/examples/demo")
    assert rec.directory == manager.workspace / "repo-a/examples/demo"
    assert next(item for item in manager.list_projects()
                if item["name"] == rec.name)["open"]


@pytest.mark.parametrize("name", [
    "", ".", "..", "../outside", "/absolute", "a//b", "a/../b", "a/./b",
    "C:/outside", "a\\b", "a/", "a:other", "a?", "a.", "a /b", "CON", "a/NUL.txt",
])
def test_reject_unsafe_or_ambiguous_project_paths(tmp_path, name):
    with pytest.raises(ProjectValidationError):
        ProjectManager(tmp_path).create(name)


def test_delete_parent_does_not_remove_nested_projects(tmp_path):
    manager = ProjectManager(tmp_path)
    write_project(manager.workspace / "repo")
    write_project(manager.workspace / "repo/child")
    with pytest.raises(ProjectValidationError, match="子工程"):
        manager.delete("repo")
    assert (manager.workspace / "repo/child/project.yaml").is_file()
    manager.delete("repo/child")
    assert (manager.workspace / "repo/project.yaml").is_file()


def test_project_symlink_cannot_escape_or_duplicate_workspace(tmp_path):
    manager = ProjectManager(tmp_path)
    outside = tmp_path / "outside"
    write_project(outside)
    link = manager.workspace / "link"
    try:
        link.symlink_to(outside, target_is_directory=True)
    except OSError as exc:
        pytest.skip(f"创建符号链接不可用: {exc}")
    assert manager.list_projects() == []
    with pytest.raises(ProjectValidationError):
        manager.get("link")


@pytest.mark.skipif(os.name != "nt", reason="仅 Windows 有目录联接")
def test_windows_junction_is_not_followed(tmp_path):
    import _winapi

    manager = ProjectManager(tmp_path)
    outside = tmp_path / "outside"
    write_project(outside)
    _winapi.CreateJunction(str(outside), str(manager.workspace / "junction"))
    assert manager.list_projects() == []
    with pytest.raises(ProjectValidationError):
        manager.get("junction")


def test_nested_project_api_lifecycle_and_resources(tmp_path):
    shutil.copytree(ROOT / "orpheus_abi", tmp_path / "orpheus_abi")
    for component in ("wav_in", "wav_out"):
        shutil.copytree(
            ROOT / "components/orpheus/builtin" / component,
            tmp_path / "components/orpheus/builtin" / component,
        )
    name = "克隆 repo/demo v1.2"
    url = project_url(name)
    sibling = "other/demo v1.2"
    with TestClient(create_app(tmp_path)) as client:
        response = client.post("/api/projects", json={"name": name})
        assert response.status_code == 201, response.text
        assert response.json()["name"] == name
        assert client.post("/api/projects", json={"name": sibling}).status_code == 201
        pdir = tmp_path / "workspace" / name
        shutil.copytree(
            ROOT / "components/orpheus/builtin/gain", pdir / "components/gain",
        )
        doc = response.json()["document"]
        doc["imports"] = ["components/gain"]
        doc["graph"] = {
            "nodes": [
                {"id": "input", "component": "orpheus.builtin.wav_in",
                 "params": {"file_path": "assets/input.wav", "channels": 1}},
                {"id": "output", "component": "orpheus.builtin.wav_out",
                 "params": {"file_path": "outputs/output.wav", "channels": 1}},
                {"id": "gain", "component": "orpheus.builtin.gain",
                 "params": {"gain_db": -3, "channels": 1}},
            ],
            "connections": [
                {"from": "input:out", "to": "gain:in"},
                {"from": "gain:out", "to": "output:in"},
            ],
        }
        doc["metadata"]["description"] = "nested"
        response = client.put(url, json=doc)
        assert response.status_code == 200, response.text
        assert client.get(url).json()["metadata"]["description"] == "nested"
        assert client.get(project_url(sibling)).json()["metadata"]["description"] == ""
        assert client.post(url + "/compile").status_code == 200
        assert client.get(url + "/components").status_code == 200
        assert client.get(url + "/components/orpheus.builtin.wav_in/readme").status_code == 200
        assert client.get(url + "/todos").status_code == 200
        assert client.get(url + "/rt/status").json()["running"] is False
        assert client.put(url + "/notes", json={"content": "断点记录"}).status_code == 200
        assert client.get(url + "/notes").json()["content"] == "断点记录"
        cloned_name = "另一仓库/models/demo"
        shutil.copytree(pdir, tmp_path / "workspace" / cloned_name)
        assert client.get(project_url(cloned_name)).json()["imports"] == ["components/gain"]
        assert client.post(project_url(cloned_name) + "/compile").status_code == 200
        response = client.post(
            url + "/uploads", files={"file": ("sample.txt", b"asset", "text/plain")},
        )
        assert response.status_code == 201, response.text
        files = client.get(url + "/files").json()
        relpath = next(item["path"] for item in files if item["path"].endswith("sample.txt"))
        assert client.get(url + "/files/" + relpath).content == b"asset"
        archive = client.get(url + "/download")
        assert archive.status_code == 200, archive.text
        response = client.post(url + "/generate")
        assert response.status_code == 200, response.text
        assert client.get(response.json()["download_url"]).status_code == 200
        assert client.delete(url).status_code == 200
        assert client.get(url).status_code == 404
        assert client.get(project_url(sibling)).status_code == 200


def test_group_directory_is_not_a_project_in_file_apis(tmp_path):
    write_project(tmp_path / "workspace/repo/models/demo")
    with TestClient(create_app(tmp_path)) as client:
        for suffix in ("", "/files", "/files/models/demo/project.yaml", "/download"):
            assert client.get(project_url("repo") + suffix).status_code == 404
        response = client.post(
            project_url("repo") + "/uploads",
            files={"file": ("sample.txt", b"asset", "text/plain")},
        )
        assert response.status_code == 404


def test_malformed_project_is_listed_with_an_explicit_error(tmp_path):
    directory = tmp_path / "workspace/repo/broken"
    directory.mkdir(parents=True)
    (directory / "project.yaml").write_text("metadata: [", encoding="utf-8")
    item, = ProjectManager(tmp_path).list_projects()
    assert item["name"] == "repo/broken"
    assert item["error"]


def test_nested_project_prevents_deleting_its_component(tmp_path):
    component = tmp_path / "components/orpheus/builtin/gain"
    shutil.copytree(ROOT / "components/orpheus/builtin/gain", component)
    manifest_path = component / "component.yaml"
    manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    manifest["user_owned"] = True
    manifest_path.write_text(yaml.safe_dump(manifest, allow_unicode=True), encoding="utf-8")
    name = "repo/models/demo"
    with TestClient(create_app(tmp_path)) as client:
        response = client.post("/api/projects", json={"name": name})
        doc = response.json()["document"]
        doc["graph"]["nodes"] = [{"id": "gain", "component": "orpheus.builtin.gain"}]
        assert client.put(project_url(name), json=doc).status_code == 200
        response = client.delete("/api/components/orpheus.builtin.gain")
        assert response.status_code == 400
        assert name in response.json()["detail"]
        assert manifest_path.is_file()


def test_project_names_matching_api_suffixes_remain_unambiguous(tmp_path):
    with TestClient(create_app(tmp_path)) as client:
        for name in ("repo", "repo/components", "repo/notes", "repo/files"):
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            response = client.get(project_url(name))
            assert response.status_code == 200, response.text
            assert response.json()["metadata"]["name"] == name
        assert isinstance(client.get(project_url("repo") + "/components").json(), list)
        assert client.put(project_url("repo") + "/notes", json={"content": "parent"}).status_code == 200
        assert client.get(project_url("repo") + "/notes").json()["content"] == "parent"
