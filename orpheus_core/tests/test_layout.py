"""布局迁移、结构字节稳定及工程/共享组件的真实保存链路。"""
from pathlib import Path
import copy
import io
import zipfile

from editor_client import EditorClient as TestClient
import jsonschema
import pytest
import yaml

from orpheus_core.compiler import GraphCompiler
from orpheus_core.layout import save_document
from orpheus_core.project import ProjectLoader, project_to_dict
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app
from orpheus_core.server.manager import ProjectManager, ProjectValidationError
from orpheus_core.subgraph import flatten_project
from orpheus_core.validation import discover_project_files
from test_component_imports import composite_manifest, project_document, write_yaml

ROOT = Path(__file__).resolve().parents[2]


def test_interface_visibility_loader_roundtrip_is_layout_only(tmp_path, registry):
    path = tmp_path / "project.yaml"
    document = position_document()
    document["layout"] = {"file": "views/custom.yaml"}
    visibility = {"main": {
        "chain": {"exposedParameters": ["gain_db"], "exposedPorts": [], "exposedBulkSlots": []},
        "deleted": {"exposedPorts": ["out"]},
    }}
    write_yaml(tmp_path / "views/custom.yaml", {"version": 1, "views": {}, "node_ui": visibility})
    write_yaml(path, document)
    loader = ProjectLoader(registry)
    project = loader.load(path)
    before = GraphCompiler(registry).compile(flatten_project(project))
    assert "deleted" not in project.node_ui["main"]
    loader.save(project, path)
    baseline = path.read_bytes()
    project.node_ui["main"]["chain"]["exposedPorts"] = ["in", "out"]
    loader.save(project, path)
    assert path.read_bytes() == baseline
    assert read(path)["layout"] == {"file": "views/custom.yaml"}
    restored = loader.load(path)
    assert restored.node_ui == project.node_ui
    assert before == GraphCompiler(registry).compile(flatten_project(restored))
    assert project_to_dict(restored, materialize_imports=True)["layout"]["node_ui"] == restored.node_ui
    exported = tmp_path / "embedded.yaml"
    loader.save(restored, exported, separate_layout=False)
    assert loader.load(exported).node_ui == restored.node_ui


def test_interface_visibility_imported_views_are_project_overrides(tmp_path, registry):
    directory = tmp_path / "workspace" / "demo"
    definition = directory / "definitions/chain.component.yaml"
    save_document(composite_manifest("test.chain", "orpheus.builtin.gain", "gain"), definition)
    write_yaml(directory / "project.yaml", project_document(
        ["definitions/chain.component.yaml"], "test.chain"))
    manager = ProjectManager(tmp_path, registry)
    doc = manager.get_document("demo")
    manager.put("demo", doc)
    baseline = {p: p.read_bytes() for p in (directory / "project.yaml", definition)}
    doc = manager.get_document("demo")
    doc["layout"] = {"version": 1, "views": {}, "node_ui": {
        "main": {"chain": {"exposedPorts": ["in"]}},
        "sub:test.chain": {"gain": {"exposedParameters": ["gain_db"], "exposedPorts": []}},
        "sub:deleted": {"bad": {"exposedPorts": ["out"]}},
    }}
    manager.put("demo", doc)
    manager.invalidate_cached_projects()
    restored = manager.get_document("demo")["layout"]["node_ui"]
    assert restored["sub:test.chain"]["gain"]["exposedPorts"] == []
    assert "sub:deleted" not in restored
    assert all(p.read_bytes() == data for p, data in baseline.items())
    restored["sub:test.chain"]["gain"]["exposedParameters"] = []
    manager.put("demo", manager.get_document("demo") | {
        "layout": {"version": 1, "views": {}, "node_ui": restored}})
    assert manager.get_document("demo")["layout"]["node_ui"]["sub:test.chain"]["gain"]["exposedParameters"] == []


def test_interface_visibility_api_validates_and_preserves_empty_arrays(tmp_path):
    with TestClient(create_app(tmp_path)) as client:
        document = client.post("/api/projects", json={"name": "demo"}).json()["document"]
        document["graph"] = {"nodes": [{"id": "node", "component": "test.node"}], "connections": []}
        document["layout"] = {"version": 1, "views": {}, "node_ui": {
            "main": {"node": {"exposedParameters": [], "exposedPorts": ["out"], "exposedBulkSlots": []}}}}
        assert client.put("/api/projects/demo", json=document).status_code == 200
        restored = client.get("/api/projects/demo").json()
        assert restored["layout"]["node_ui"] == document["layout"]["node_ui"]
        assert client.put("/api/projects/demo", json=restored).status_code == 200
        baseline = (tmp_path / "workspace/demo/project.yaml").read_bytes()
        restored["layout"]["node_ui"]["main"]["node"]["exposedPorts"] = []
        assert client.put("/api/projects/demo", json=restored).status_code == 200
        assert client.get("/api/projects/demo").json()["layout"]["node_ui"]["main"]["node"]["exposedPorts"] == []
        assert (tmp_path / "workspace/demo/project.yaml").read_bytes() == baseline
        restored["layout"]["node_ui"]["main"]["node"]["exposedPorts"] = ["out", "out"]
        assert client.put("/api/projects/demo", json=restored).status_code == 400


@pytest.fixture(scope="module")
def registry():
    result = Registry([ROOT / "components"])
    result.scan()
    return result


def position_document():
    document = project_document([], "orpheus.builtin.gain")
    document["graph"]["nodes"][1]["params"] = {"channels": 1}
    for index, node in enumerate(document["graph"]["nodes"]):
        node["position"] = {"x": index * 123 + 0.5, "y": -17}
    document["bridges"] = [{
        "id": "device", "transport": "uart", "params": {"port": "COM3"},
        "position": {"x": -83, "y": 42},
    }]
    return document


def read(path):
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def test_loader_migrates_embedded_positions_and_defaults_to_sidecar(tmp_path, registry):
    path = tmp_path / "project.yaml"
    original = position_document()
    original["subcomponents"] = [{
        "id": "legacy", "ports": [],
        "graph": {"nodes": [{"id": "gain", "component": "orpheus.builtin.gain",
                             "position": {"x": 51, "y": -2}}], "connections": []},
    }]
    write_yaml(path, original)
    loader = ProjectLoader(registry)
    project = loader.load(path)
    before = GraphCompiler(registry).compile(flatten_project(project))
    loader.save(project, path)
    stored = read(path)
    assert stored["layout"] == {"file": "project.layout.yaml"}
    assert all("position" not in node for node in stored["graph"]["nodes"])
    assert "position" not in stored["bridges"][0]
    assert "position" not in stored["subcomponents"][0]["graph"]["nodes"][0]
    layout = read(tmp_path / "project.layout.yaml")
    assert layout["views"]["main"]["bridges"]["device"] == {"x": -83, "y": 42}
    assert layout["views"]["sub:legacy"]["nodes"]["gain"] == {"x": 51, "y": -2}
    loaded = loader.load(path)
    assert loaded.graph.nodes["source"].position == original["graph"]["nodes"][0]["position"]
    assert loaded.subcomponents[0].graph.nodes["gain"].position == {"x": 51, "y": -2}
    after = GraphCompiler(registry).compile(flatten_project(loaded))
    assert before == after
    loaded.graph.nodes["source"].position = {"x": 900, "y": 901}
    assert before == GraphCompiler(registry).compile(flatten_project(loaded))


def test_layout_only_save_preserves_exact_structure_bytes_and_mtime(tmp_path, registry):
    path = tmp_path / "project.yaml"
    write_yaml(path, position_document())
    loader = ProjectLoader(registry)
    loader.save(loader.load(path), path)
    # 即使已有结构文件含人工注释，移动节点也不能重新序列化它。
    path.write_text("# 保留此结构注释\n" + path.read_text(encoding="utf-8"), encoding="utf-8")
    baseline = path.read_bytes()
    mtime = path.stat().st_mtime_ns
    project = loader.load(path)
    project.graph.nodes["source"].position = {"x": 999, "y": 111}
    project.bridges[0].position = {"x": 6, "y": 7}
    loader.save(project, path)
    assert path.read_bytes() == baseline
    assert path.stat().st_mtime_ns == mtime
    assert loader.load(path).graph.nodes["source"].position == {"x": 999, "y": 111}
    assert loader.load(path).bridges[0].position == {"x": 6, "y": 7}
    assert b"\r" not in (tmp_path / "project.layout.yaml").read_bytes()


@pytest.mark.parametrize("external", [False, True])
def test_explicit_inline_or_external_layout_overlays_embedded_positions(tmp_path, registry, external):
    path = tmp_path / "project.yaml"
    document = position_document()
    layout = {"version": 1, "views": {"main": {"nodes": {
        "source": {"x": 200, "y": 300}, "deleted": {"x": 7, "y": 8},
    }}}}
    if external:
        write_yaml(tmp_path / "views" / "custom.yaml", layout)
        document["layout"] = {"file": "views/custom.yaml"}
    else:
        document["layout"] = layout
    write_yaml(path, document)
    loader = ProjectLoader(registry)
    project = loader.load(path)
    assert project.graph.nodes["source"].position == {"x": 200, "y": 300}
    assert project.graph.nodes["sink"].position == document["graph"]["nodes"][2]["position"]
    loader.save(project, path)
    target = tmp_path / read(path)["layout"]["file"]
    assert "deleted" not in read(target)["views"]["main"]["nodes"]
    assert all("position" not in n for n in read(path)["graph"]["nodes"])


@pytest.mark.parametrize("external", [False, True])
def test_stale_node_bridge_and_subgraph_layout_entries_are_ignored(tmp_path, registry, external):
    directory = tmp_path / "workspace" / "demo"
    definition = directory / "definitions" / "chain.component.yaml"
    manifest = composite_manifest("test.chain", "orpheus.builtin.gain", "gain")
    component_layout = {"version": 1, "views": {"main": {"nodes": {
        "gain": {"x": 1, "y": 2}, "deleted_component_node": {"x": 3, "y": 4},
    }}}}
    document = position_document()
    document["imports"] = ["definitions/chain.component.yaml"]
    document["subcomponents"] = [{
        "id": "legacy", "ports": [],
        "graph": {"nodes": [{"id": "inner", "component": "orpheus.builtin.gain",
                             "params": {"channels": 1}}], "connections": []},
    }]
    layout = {"version": 1, "views": {
        "main": {
            "nodes": {"source": {"x": 10, "y": 20}, "deleted_node": {"x": 3, "y": 4}},
            "bridges": {"device": {"x": 30, "y": 40}, "deleted_bridge": {"x": 3, "y": 4}},
        },
        "sub:legacy": {"nodes": {
            "inner": {"x": 50, "y": 60}, "deleted_inner": {"x": 3, "y": 4},
        }},
        "sub:test.chain": {"nodes": {
            "gain": {"x": 70, "y": 80}, "deleted_imported": {"x": 3, "y": 4},
        }},
        "sub:deleted_subgraph": {"nodes": {"missing": {"x": 3, "y": 4}}},
    }}
    if external:
        write_yaml(directory / "project.layout.yaml", layout)
        document["layout"] = {"file": "project.layout.yaml"}
        write_yaml(definition.with_name("chain.component.layout.yaml"), component_layout)
        manifest["layout"] = {"file": "chain.component.layout.yaml"}
    else:
        document["layout"] = layout
        manifest["layout"] = component_layout
    write_yaml(definition, manifest)
    write_yaml(directory / "project.yaml", document)
    manager = ProjectManager(tmp_path, registry)
    project = manager.get("demo").project
    assert set(project.graph.nodes) == {"source", "chain", "sink"}
    assert project.graph.nodes["source"].position == {"x": 10, "y": 20}
    assert [bridge.id for bridge in project.bridges] == ["device"]
    assert project.bridges[0].position == {"x": 30, "y": 40}
    subs = {sub.id: sub for sub in project.subcomponents}
    assert set(subs) == {"legacy", "test.chain"}
    assert set(subs["legacy"].graph.nodes) == {"inner"}
    assert subs["legacy"].graph.nodes["inner"].position == {"x": 50, "y": 60}
    assert set(subs["test.chain"].graph.nodes) == {"gain"}
    assert subs["test.chain"].graph.nodes["gain"].position == {"x": 70, "y": 80}
    manager.put("demo", manager.get_document("demo"))
    manager.invalidate_cached_projects()
    assert manager.get("demo").project.graph.nodes["source"].position == {"x": 10, "y": 20}
    saved = read(directory / "project.layout.yaml")["views"]
    assert set(saved) == {"main", "sub:legacy"}
    assert set(saved["main"]["nodes"]) == {"source", "chain", "sink"}
    assert set(saved["main"]["bridges"]) == {"device"}
    assert set(saved["sub:legacy"]["nodes"]) == {"inner"}
    assert set(read(definition.with_name("chain.component.layout.yaml"))["views"]["main"]["nodes"]) == {"gain"}


def test_explicit_embedded_save_still_supported(tmp_path, registry):
    path = tmp_path / "project.yaml"
    write_yaml(path, position_document())
    loader = ProjectLoader(registry)
    loader.save(loader.load(path), path)
    project = loader.load(path)
    project.graph.nodes["source"].position = {"x": 10, "y": 11}
    exported = tmp_path / "export" / "embedded.yaml"
    loader.save(project, exported, separate_layout=False)
    assert loader.load(exported).graph.nodes["source"].position == {"x": 10, "y": 11}
    assert "layout" not in read(exported)
    loader.save(project, path, separate_layout=False)
    assert "layout" not in read(path)
    assert read(path)["graph"]["nodes"][0]["position"] == {"x": 10, "y": 11}
    assert loader.load(path).graph.nodes["source"].position == {"x": 10, "y": 11}


def test_saving_explicit_inline_map_as_embedded_does_not_drop_positions(tmp_path, registry):
    document = position_document()
    document["graph"]["nodes"][0].pop("position")
    document["layout"] = {"version": 1, "views": {"main": {
        "nodes": {"source": {"x": 123, "y": 456}},
    }}}
    path = tmp_path / "project.yaml"
    save_document(document, path, separate_layout=False)
    assert "layout" not in read(path)
    assert ProjectLoader(registry).load(path).graph.nodes["source"].position == {"x": 123, "y": 456}


def test_manager_separates_component_layouts_and_only_rewrites_layouts_on_move(tmp_path, registry):
    project_dir = tmp_path / "workspace" / "demo"
    definition = project_dir / "definitions" / "chain.component.yaml"
    manifest = composite_manifest("test.chain", "orpheus.builtin.gain", "gain")
    manifest["graph"]["nodes"][0]["position"] = {"x": 1, "y": 2}
    write_yaml(definition, manifest)
    document = position_document()
    document["imports"] = ["definitions/chain.component.yaml"]
    document["graph"]["nodes"][1]["component"] = "test.chain"
    write_yaml(project_dir / "project.yaml", document)
    manager = ProjectManager(tmp_path, registry)
    manager.put("demo", manager.get_document("demo"))
    assert read(definition)["layout"] == {"file": "chain.component.layout.yaml"}
    assert "position" not in read(definition)["graph"]["nodes"][0]
    snapshots = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in
                 (project_dir / "project.yaml", definition)}
    moved = manager.get_document("demo")
    moved["graph"]["nodes"][1]["position"] = {"x": 301, "y": 402}
    moved["subcomponents"][0]["graph"]["nodes"][0]["position"] = {"x": 901, "y": 802}
    manager.put("demo", moved)
    manager.invalidate_cached_projects()
    restored = manager.get_document("demo")
    assert restored["graph"]["nodes"][1]["position"] == {"x": 301, "y": 402}
    assert restored["subcomponents"][0]["graph"]["nodes"][0]["position"] == {"x": 901, "y": 802}
    for path, (data, mtime) in snapshots.items():
        assert path.read_bytes() == data
        assert path.stat().st_mtime_ns == mtime
    restored["subcomponents"][0]["graph"]["nodes"][0]["params"]["gain_db"] = -9
    manager.put("demo", restored)
    assert read(definition)["graph"]["nodes"][0]["params"]["gain_db"] == -9
    assert "position" not in read(definition)["graph"]["nodes"][0]


def test_public_component_layout_override_does_not_modify_library(tmp_path, registry):
    library = tmp_path / "library" / "chain.component.yaml"
    manifest = composite_manifest("test.public_chain", "orpheus.builtin.gain", "gain")
    manifest["graph"]["nodes"][0]["position"] = {"x": 10, "y": 20}
    save_document(manifest, library)
    local_registry = registry.fork()
    local_registry.add_manifest(library)
    project_dir = tmp_path / "workspace" / "demo"
    write_yaml(project_dir / "project.yaml", project_document(
        [{"component": "test.public_chain"}], "test.public_chain"))
    manager = ProjectManager(tmp_path, local_registry)
    doc = manager.get_document("demo")
    sub = doc["subcomponents"][0]
    assert sub["read_only"]
    assert sub["graph"]["nodes"][0]["position"] == {"x": 10, "y": 20}
    snapshots = {path: path.read_bytes() for path in library.parent.iterdir()}
    sub["graph"]["nodes"][0]["position"] = {"x": 777, "y": 888}
    manager.put("demo", doc)
    for path, data in snapshots.items():
        assert path.read_bytes() == data
    manager.invalidate_cached_projects()
    assert manager.get_document("demo")["subcomponents"][0]["graph"]["nodes"][0]["position"] == {"x": 777, "y": 888}
    loader = ProjectLoader(local_registry)
    project = loader.load(project_dir)
    loader.save(project, project_dir / "project.yaml")
    assert loader.load(project_dir).subcomponents[0].graph.nodes["gain"].position == {"x": 777, "y": 888}
    project.subcomponents[0].graph.nodes["gain"].position = {}
    loader.save(project, project_dir / "project.yaml", separate_layout=False)
    assert loader.load(project_dir).subcomponents[0].graph.nodes["gain"].position == {}


def test_shared_component_sidecar_refreshes_parent_and_nested_cached_views(tmp_path, registry):
    parent = tmp_path / "workspace" / "demo"
    child = parent / "nested"
    definition = child / "components" / "chain.component.yaml"
    save_document(composite_manifest("test.shared", "orpheus.builtin.gain", "gain"), definition)
    write_yaml(parent / "project.yaml", project_document(
        ["nested/components/chain.component.yaml"], "test.shared"))
    write_yaml(child / "project.yaml", project_document(
        ["components/chain.component.yaml"], "test.shared"))
    manager = ProjectManager(tmp_path, registry)
    manager.get_document("demo")
    doc = manager.get_document("demo/nested")
    doc["subcomponents"][0]["graph"]["nodes"][0]["position"] = {"x": 900, "y": 800}
    manager.put("demo/nested", doc)
    assert manager.get_document("demo")["subcomponents"][0]["graph"]["nodes"][0]["position"] == {"x": 900, "y": 800}
    # 外部修改旁文件，不能依赖根文件时间戳或本进程保存事件。
    layout_path = definition.with_name("chain.component.layout.yaml")
    layout = read(layout_path)
    layout["views"]["main"]["nodes"]["gain"] = {"x": 8765, "y": 4321}
    write_yaml(layout_path, layout)
    for name in ("demo", "demo/nested"):
        assert manager.get_document(name)["subcomponents"][0]["graph"]["nodes"][0]["position"] == {"x": 8765, "y": 4321}
    scanned = Registry([child / "components"])
    scanned.scan()
    assert [info.id for info in scanned.list_components()] == ["test.shared"]
    assert set(discover_project_files([tmp_path / "workspace"])) == {
        parent / "project.yaml", child / "project.yaml",
    }


def test_public_component_default_layout_changes_refresh_cached_projects(tmp_path, registry):
    path = tmp_path / "library" / "chain.component.yaml"
    save_document(composite_manifest("test.public", "orpheus.builtin.gain", "gain"), path)
    local_registry = registry.fork()
    local_registry.add_manifest(path)
    directory = tmp_path / "workspace" / "demo"
    write_yaml(directory / "project.yaml", project_document(
        [{"component": "test.public"}], "test.public"))
    manager = ProjectManager(tmp_path, local_registry)
    manager.put("demo", manager.get_document("demo"))
    layout_path = path.with_name("chain.component.layout.yaml")
    layout = read(layout_path)
    layout["views"]["main"]["nodes"]["gain"] = {"x": 6789, "y": 10}
    write_yaml(layout_path, layout)
    assert manager.get_document("demo")["subcomponents"][0]["graph"]["nodes"][0]["position"] == {"x": 6789, "y": 10}


def test_saving_inline_layout_map_for_materialized_composite_keeps_positions(tmp_path, registry):
    directory = tmp_path / "workspace" / "demo"
    definition = directory / "chain.component.yaml"
    write_yaml(definition, composite_manifest("test.chain", "orpheus.builtin.gain", "gain"))
    write_yaml(directory / "project.yaml", project_document(["chain.component.yaml"], "test.chain"))
    manager = ProjectManager(tmp_path, registry)
    doc = manager.get_document("demo")
    doc["subcomponents"][0]["graph"]["nodes"][0].pop("position")
    doc["layout"] = {"version": 1, "views": {
        "sub:test.chain": {"nodes": {"gain": {"x": 555, "y": 666}}},
    }}
    manager.put("demo", doc)
    assert manager.get_document("demo")["subcomponents"][0]["graph"]["nodes"][0]["position"] == {"x": 555, "y": 666}
    assert "sub:test.chain" not in read(directory / "project.layout.yaml")["views"]
    assert read(directory / "chain.component.layout.yaml")["views"]["main"]["nodes"]["gain"] == {"x": 555, "y": 666}


def test_automatic_project_discovery_excludes_standalone_layout_files(tmp_path, registry):
    path = tmp_path / "standalone.yaml"
    save_document(position_document(), path)
    write_yaml(tmp_path / "unrelated.layout.yml", {"version": 1, "views": {}})
    assert discover_project_files([tmp_path]) == [path]
    assert ProjectLoader(registry).load(path).graph.nodes["source"].position == {"x": 0.5, "y": -17}


def test_default_sidecar_does_not_overwrite_unrelated_file(tmp_path):
    path = tmp_path / "project.yaml"
    sidecar = tmp_path / "project.layout.yaml"
    write_yaml(sidecar, {"unrelated": "keep"})
    snapshot = sidecar.read_bytes()
    with pytest.raises(jsonschema.ValidationError):
        save_document(position_document(), path)
    assert sidecar.read_bytes() == snapshot
    assert not path.exists()


def test_layout_symlink_cannot_escape_owner_directory(tmp_path, registry):
    outside = tmp_path / "outside.yaml"
    write_yaml(outside, {"version": 1, "views": {}})
    directory = tmp_path / "project"
    directory.mkdir()
    target = directory / "project.layout.yaml"
    try:
        target.symlink_to(outside)
    except OSError as exc:
        pytest.skip(f"创建符号链接不可用：{exc}")
    document = position_document()
    document["layout"] = {"file": "project.layout.yaml"}
    write_yaml(directory / "project.yaml", document)
    with pytest.raises(ValueError, match="越出"):
        ProjectLoader(registry).load(directory)
    with pytest.raises(ValueError, match="越出"):
        save_document(position_document(), directory / "project.yaml")


def test_cached_projects_reload_external_layout_changes_and_report_missing_file(tmp_path, registry):
    directory = tmp_path / "workspace" / "demo"
    write_yaml(directory / "project.yaml", position_document())
    manager = ProjectManager(tmp_path, registry)
    manager.put("demo", manager.get_document("demo"))
    path = directory / "project.layout.yaml"
    layout = read(path)
    layout["views"]["main"]["nodes"]["source"] = {"x": 3333, "y": 4444}
    write_yaml(path, layout)
    assert manager.get_document("demo")["graph"]["nodes"][0]["position"] == {"x": 3333, "y": 4444}
    path.unlink()
    with pytest.raises(ProjectValidationError, match="布局"):
        manager.get_document("demo")


@pytest.mark.parametrize("patch", [
    {"layout": {"file": "../escape.yaml"}},
    {"layout": {"file": "project.yaml"}},
    {"layout": {"file": ""}},
    {"layout": {"file": "C:\\outside\\layout.yaml"}},
    {"layout": {"file": "C:layout.yaml"}},
    {"layout": {"file": 3}},
    {"layout": {"version": 1, "views": {"main": {"nodes": {"source": {"x": "bad"}}}}}},
    {"layout": {"version": 1, "views": {"main": {"nodes": {"source": {"x": float("inf")}}}}}},
])
def test_malformed_layout_is_explicit_error(tmp_path, registry, patch):
    document = position_document()
    document.update(patch)
    write_yaml(tmp_path / "project.yaml", document)
    with pytest.raises((ValueError, jsonschema.ValidationError)):
        ProjectLoader(registry).load(tmp_path)


def test_api_layout_roundtrip_and_bad_position_returns_400(tmp_path):
    with TestClient(create_app(tmp_path)) as client:
        response = client.post("/api/projects", json={"name": "demo"})
        assert response.status_code == 201
        doc = response.json()["document"]
        doc["graph"] = {"nodes": [{"id": "n", "component": "test.node",
                                    "position": {"x": 23, "y": 45}}], "connections": []}
        assert client.put("/api/projects/demo", json=doc).status_code == 200
        assert client.get("/api/projects/demo").json()["graph"]["nodes"][0]["position"] == {"x": 23, "y": 45}
        directory = tmp_path / "workspace" / "demo"
        baseline = (directory / "project.yaml").read_bytes()
        doc["graph"]["nodes"][0]["position"] = {"x": 67, "y": 89}
        assert client.put("/api/projects/demo", json=doc).status_code == 200
        assert (directory / "project.yaml").read_bytes() == baseline
        bad = copy.deepcopy(doc)
        bad["layout"] = {"file": "../escape.yaml"}
        response = client.put("/api/projects/demo", json=bad)
        assert response.status_code == 400, response.text
        assert (directory / "project.yaml").read_bytes() == baseline
        response = client.get("/api/projects/demo/download")
        with zipfile.ZipFile(io.BytesIO(response.content)) as archive:
            assert {"project.yaml", "project.layout.yaml"} <= set(archive.namelist())
        (directory / "project.layout.yaml").unlink()
        response = client.get("/api/projects/demo")
        assert response.status_code == 400, response.text
        assert "布局" in response.json()["detail"]
