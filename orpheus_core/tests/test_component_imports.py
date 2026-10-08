"""Directory project and uniform component import tests."""

from __future__ import annotations

from pathlib import Path
import hashlib
import struct
import subprocess

import pytest
import yaml
from fastapi.testclient import TestClient

from orpheus_core.compiler import GraphCompiler
from orpheus_core.builder import ComponentBuilder
from orpheus_core.project import ProjectLoader, project_to_dict
from orpheus_core.registry import Registry
from orpheus_core.server.manager import ProjectManager
from orpheus_core.server.app import create_app
from orpheus_core.subgraph import flatten_project
from orpheus_core.validation import discover_project_files, validate_project

ROOT = Path(__file__).resolve().parents[2]


def write_yaml(path: Path, document: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        yaml.safe_dump(document, sort_keys=False, allow_unicode=True),
        encoding="utf-8",
    )


def atomic_manifest(component_id: str) -> dict:
    return {
        "id": component_id,
        "name": component_id,
        "category": "高级/测试",
        "version": "1.0.0",
        "abi_version": 1,
        "package_type": "source",
        "ports": [
            {"id": "in", "direction": "input", "type": "audio", "channels": 1},
            {"id": "out", "direction": "output", "type": "audio", "channels": 1},
        ],
        "parameters": [],
        "execution": {"sample_rate_independent": True, "realtime_safe": True},
    }


def composite_manifest(
    component_id: str,
    child_component: str,
    child_id: str,
    *,
    imports: list | None = None,
) -> dict:
    document = {
        "id": component_id,
        "name": component_id,
        "category": "高级/测试",
        "version": "1.0.0",
        "abi_version": 1,
        "package_type": "composite",
        "ports": [
            {
                "id": "in",
                "direction": "input",
                "type": "audio",
                "channels": 1,
                "maps_to": f"{child_id}:in",
            },
            {
                "id": "out",
                "direction": "output",
                "type": "audio",
                "channels": 1,
                "maps_to": f"{child_id}:out",
            },
        ],
        "parameters": [],
        "graph": {
            "nodes": [
                {
                    "id": child_id,
                    "component": child_component,
                    "params": {"channels": 1},
                }
            ],
            "connections": [],
        },
    }
    if imports:
        document["imports"] = imports
    return document


def project_document(imports: list, component_id: str) -> dict:
    return {
        "version": "1.0.0",
        "metadata": {"name": "imports"},
        "sample_rate": 48000,
        "block_size": 32,
        "imports": imports,
        "graph": {
            "nodes": [
                {
                    "id": "source",
                    "component": "orpheus.builtin.signal_gen",
                    "params": {"channels": 1, "duration_s": 0.01},
                },
                {"id": "chain", "component": component_id, "params": {}},
                {
                    "id": "sink",
                    "component": "orpheus.builtin.null_sink",
                    "params": {"channels": 1},
                },
            ],
            "connections": [
                {"from": "source:out", "to": "chain:in"},
                {"from": "chain:out", "to": "sink:in"},
            ],
        },
    }


def builtin_registry() -> Registry:
    registry = Registry([ROOT / "components"])
    registry.scan()
    return registry


def test_directory_project_recursively_imports_and_flattens_bare_component_ids(
    tmp_path: Path,
) -> None:
    project_dir = tmp_path / "project"
    components = project_dir / "definitions"
    write_yaml(
        components / "inner.component.yaml",
        composite_manifest("test.inner", "orpheus.builtin.gain", "gain"),
    )
    write_yaml(
        components / "outer.component.yaml",
        composite_manifest(
            "test.outer",
            "test.inner",
            "inner",
            imports=["inner.component.yaml"],
        ),
    )
    write_yaml(
        project_dir / "project.yaml",
        project_document(["definitions/outer.component.yaml"], "test.outer"),
    )

    project = ProjectLoader(builtin_registry()).load(project_dir)
    assert project.root_dir == project_dir.resolve()
    assert {sub.id for sub in project.subcomponents} == {"test.inner", "test.outer"}
    assert project.registry is not None
    assert project.registry.get("test.outer").package_type == "composite"

    flat = flatten_project(project)
    assert "chain__inner__gain" in flat.graph.nodes
    assert flat.graph.nodes["chain__inner__gain"].component == "orpheus.builtin.gain"
    assert any(
        str(connection.to_ref) == "chain__inner__gain:in"
        for connection in flat.graph.connections
    )
    plan = GraphCompiler(project.registry).compile(flat)
    assert "chain__inner__gain" in plan.node_configs


def test_public_parameter_can_map_through_direct_child_composite(tmp_path: Path) -> None:
    project_dir = tmp_path / "project"
    inner = composite_manifest("test.inner", "orpheus.builtin.gain", "gain")
    inner["parameters"] = [
        {
            "id": "gain_db",
            "name": "增益",
            "type": "float",
            "direction": "input",
            "maps_to": "gain:gain_db",
            "default": 0.0,
            "update_policy": "smoothed",
        }
    ]
    outer = composite_manifest(
        "test.outer", "test.inner", "inner", imports=["inner.component.yaml"]
    )
    outer["parameters"] = [
        {
            "id": "gain_db",
            "name": "增益",
            "type": "float",
            "direction": "input",
            "maps_to": "inner:gain_db",
            "default": -3.0,
            "update_policy": "smoothed",
        }
    ]
    write_yaml(project_dir / "definitions" / "inner.component.yaml", inner)
    write_yaml(project_dir / "definitions" / "outer.component.yaml", outer)
    document = project_document(["definitions/outer.component.yaml"], "test.outer")
    document["graph"]["nodes"][1]["params"] = {"gain_db": -12.0}
    write_yaml(project_dir / "project.yaml", document)
    project = ProjectLoader(builtin_registry()).load(project_dir)
    flat = flatten_project(project)
    assert flat.graph.nodes["chain__inner__gain"].params["gain_db"] == -12.0


def test_local_source_component_uses_same_graph_reference_syntax(tmp_path: Path) -> None:
    project_dir = tmp_path / "project"
    write_yaml(project_dir / "components" / "local.component.yaml", atomic_manifest("test.local"))
    write_yaml(
        project_dir / "project.yaml",
        project_document(["components/local.component.yaml"], "test.local"),
    )
    project = ProjectLoader(builtin_registry()).load(project_dir)
    assert project.registry is not None
    assert project.registry.get("test.local").package_type == "source"
    plan = GraphCompiler(project.registry).compile(flatten_project(project))
    assert plan.node_configs["chain"]["component"] == "test.local"


def test_global_composite_can_be_imported_by_component_id(tmp_path: Path) -> None:
    global_manifest = tmp_path / "global" / "global.component.yaml"
    write_yaml(
        global_manifest,
        composite_manifest("test.global", "orpheus.builtin.gain", "gain"),
    )
    registry = builtin_registry()
    registry.add_manifest(global_manifest)
    project_dir = tmp_path / "project"
    write_yaml(
        project_dir / "project.yaml",
        project_document([{"component": "test.global"}], "test.global"),
    )
    project = ProjectLoader(registry).load(project_dir)
    assert {sub.id for sub in project.subcomponents} == {"test.global"}
    assert "chain__gain" in flatten_project(project).graph.nodes


def test_global_component_version_pin_is_enforced(tmp_path: Path) -> None:
    global_manifest = tmp_path / "global" / "global.component.yaml"
    write_yaml(
        global_manifest,
        composite_manifest("test.global", "orpheus.builtin.gain", "gain"),
    )
    registry = builtin_registry()
    registry.add_manifest(global_manifest)
    project_dir = tmp_path / "project"
    write_yaml(
        project_dir / "project.yaml",
        project_document(
            [{"component": "test.global", "version": "9.9.9"}], "test.global"
        ),
    )
    with pytest.raises(ValueError, match="version mismatch"):
        ProjectLoader(registry).load(project_dir)


def test_global_composite_recursively_imports_global_child(tmp_path: Path) -> None:
    package_dir = tmp_path / "global"
    inner_path = package_dir / "inner.component.yaml"
    outer_path = package_dir / "outer.component.yaml"
    write_yaml(
        inner_path,
        composite_manifest("test.global_inner", "orpheus.builtin.gain", "gain"),
    )
    outer = composite_manifest("test.global_outer", "test.global_inner", "inner")
    outer["imports"] = [{"component": "test.global_inner"}]
    write_yaml(outer_path, outer)
    registry = builtin_registry()
    registry.add_manifest(inner_path)
    registry.add_manifest(outer_path)
    project_dir = tmp_path / "project"
    write_yaml(
        project_dir / "project.yaml",
        project_document([{"component": "test.global_outer"}], "test.global_outer"),
    )
    project = ProjectLoader(registry).load(project_dir)
    assert {sub.id for sub in project.subcomponents} == {
        "test.global_inner",
        "test.global_outer",
    }
    assert "chain__inner__gain" in flatten_project(project).graph.nodes


def test_global_component_import_cycle_is_rejected(tmp_path: Path) -> None:
    package_dir = tmp_path / "global"
    first = composite_manifest("test.first", "orpheus.builtin.gain", "gain")
    second = composite_manifest("test.second", "orpheus.builtin.gain", "gain")
    first["imports"] = [{"component": "test.second"}]
    second["imports"] = [{"component": "test.first"}]
    first_path = package_dir / "first.component.yaml"
    second_path = package_dir / "second.component.yaml"
    write_yaml(first_path, first)
    write_yaml(second_path, second)
    registry = builtin_registry()
    registry.add_manifest(first_path)
    registry.add_manifest(second_path)
    project_dir = tmp_path / "project"
    write_yaml(
        project_dir / "project.yaml",
        project_document([{"component": "test.first"}], "test.first"),
    )
    with pytest.raises(ValueError, match="component import cycle"):
        ProjectLoader(registry).load(project_dir)


def test_component_import_cannot_escape_project_directory(tmp_path: Path) -> None:
    write_yaml(tmp_path / "outside.component.yaml", atomic_manifest("test.outside"))
    project_dir = tmp_path / "project"
    write_yaml(
        project_dir / "project.yaml",
        project_document(["../outside.component.yaml"], "test.outside"),
    )
    with pytest.raises(ValueError, match="escapes project directory"):
        ProjectLoader(builtin_registry()).load(project_dir)


def test_duplicate_imported_component_ids_are_rejected(tmp_path: Path) -> None:
    project_dir = tmp_path / "project"
    write_yaml(project_dir / "a.component.yaml", atomic_manifest("test.duplicate"))
    write_yaml(project_dir / "b.component.yaml", atomic_manifest("test.duplicate"))
    write_yaml(
        project_dir / "project.yaml",
        project_document(["a.component.yaml", "b.component.yaml"], "test.duplicate"),
    )
    with pytest.raises(ValueError, match="duplicate component id"):
        ProjectLoader(builtin_registry()).load(project_dir)


def test_materialized_composite_edits_write_back_to_definition_file(tmp_path: Path) -> None:
    root = tmp_path / "repo"
    project_dir = root / "workspace" / "demo"
    definition = project_dir / "components" / "chain.component.yaml"
    write_yaml(
        definition,
        composite_manifest("test.chain", "orpheus.builtin.gain", "gain"),
    )
    write_yaml(
        project_dir / "project.yaml",
        project_document(["components/chain.component.yaml"], "test.chain"),
    )
    registry = Registry([ROOT / "components"])
    registry.scan()
    manager = ProjectManager(root, registry)
    document = manager.get_document("demo")
    assert document["subcomponents"][0]["id"] == "test.chain"
    document["subcomponents"][0]["name"] = "Edited Chain"
    document["subcomponents"][0]["graph"]["nodes"][0]["params"]["gain_db"] = -9.0
    manager.put("demo", document)

    persisted_root = yaml.safe_load((project_dir / "project.yaml").read_text(encoding="utf-8"))
    persisted_component = yaml.safe_load(definition.read_text(encoding="utf-8"))
    assert "subcomponents" not in persisted_root
    assert persisted_component["name"] == "Edited Chain"
    assert persisted_component["graph"]["nodes"][0]["params"]["gain_db"] == -9.0
    assert manager.get_document("demo")["subcomponents"][0]["name"] == "Edited Chain"


def test_project_save_does_not_modify_global_composite(tmp_path: Path) -> None:
    root = tmp_path / "repo"
    global_path = root / "global" / "chain.component.yaml"
    write_yaml(
        global_path,
        composite_manifest("test.global_chain", "orpheus.builtin.gain", "gain"),
    )
    registry = Registry([ROOT / "components"])
    registry.scan()
    registry.add_manifest(global_path)
    project_dir = root / "workspace" / "demo"
    write_yaml(
        project_dir / "project.yaml",
        project_document([{"component": "test.global_chain"}], "test.global_chain"),
    )
    manager = ProjectManager(root, registry)
    document = manager.get_document("demo")
    assert document["subcomponents"][0]["read_only"] is True
    document["subcomponents"][0]["name"] = "Must Not Persist"
    manager.put("demo", document)
    persisted = yaml.safe_load(global_path.read_text(encoding="utf-8"))
    assert persisted["name"] == "test.global_chain"


def test_new_dotted_composite_is_externalized_on_save(tmp_path: Path) -> None:
    root = tmp_path / "repo"
    project_dir = root / "workspace" / "demo"
    write_yaml(project_dir / "project.yaml", project_document([], "orpheus.builtin.gain"))
    registry = Registry([ROOT / "components"])
    registry.scan()
    manager = ProjectManager(root, registry)
    document = manager.get_document("demo")
    document["subcomponents"] = [
        {
            "id": "demo.chain",
            "name": "Demo Chain",
            "ports": [],
            "graph": {"nodes": [], "connections": []},
        }
    ]
    manager.put("demo", document)
    root_document = yaml.safe_load((project_dir / "project.yaml").read_text(encoding="utf-8"))
    assert root_document["imports"] == ["components/demo.chain.component.yaml"]
    assert "subcomponents" not in root_document
    assert (project_dir / "components" / "demo.chain.component.yaml").is_file()


def test_f32_resource_is_materialized_for_compile_and_serialized_as_reference(
    tmp_path: Path,
) -> None:
    project_dir = tmp_path / "project"
    raw = struct.pack("<3f", 0.25, -0.5, 1.0)
    resource_path = project_dir / "assets" / "fir.f32"
    resource_path.parent.mkdir(parents=True)
    resource_path.write_bytes(raw)
    document = {
        "version": "1.0.0",
        "resources": {
            "demo.fir": {
                "file": "assets/fir.f32",
                "format": "f32le",
                "shape": [3],
                "sha256": hashlib.sha256(raw).hexdigest(),
            }
        },
        "graph": {
            "nodes": [
                {
                    "id": "source",
                    "component": "orpheus.builtin.signal_gen",
                    "params": {"channels": 1},
                },
                {
                    "id": "fir",
                    "component": "orpheus.builtin.fir",
                    "params": {"channels": 1, "coefficients": {"$resource": "demo.fir"}},
                },
                {
                    "id": "sink",
                    "component": "orpheus.builtin.null_sink",
                    "params": {"channels": 1},
                },
            ],
            "connections": [
                {"from": "source:out", "to": "fir:in"},
                {"from": "fir:out", "to": "sink:in"},
            ],
        },
    }
    write_yaml(project_dir / "project.yaml", document)
    registry = builtin_registry()
    project = ProjectLoader(registry).load(project_dir)
    assert project.graph.nodes["fir"].params["coefficients"] == "0.25,-0.5,1"
    serialized = project_to_dict(project)
    fir_node = next(node for node in serialized["graph"]["nodes"] if node["id"] == "fir")
    assert fir_node["params"]["coefficients"] == {"$resource": "demo.fir"}
    plan = GraphCompiler(project.registry or registry).compile(flatten_project(project))
    assert plan.node_configs["fir"]["params"]["coefficients"] == "0.25,-0.5,1"


@pytest.mark.parametrize(
    ("resource_patch", "message"),
    [
        ({"shape": [4]}, "requires 4 values"),
        ({"sha256": "0" * 64}, "sha256 mismatch"),
    ],
)
def test_resource_shape_and_hash_are_verified(
    tmp_path: Path, resource_patch: dict, message: str
) -> None:
    project_dir = tmp_path / "project"
    raw = struct.pack("<3f", 1.0, 2.0, 3.0)
    (project_dir / "assets").mkdir(parents=True)
    (project_dir / "assets" / "values.f32").write_bytes(raw)
    definition = {
        "file": "assets/values.f32",
        "format": "f32le",
        "shape": [3],
        "sha256": hashlib.sha256(raw).hexdigest(),
        **resource_patch,
    }
    write_yaml(
        project_dir / "project.yaml",
        {
            "version": "1.0.0",
            "resources": {"demo.values": definition},
            "graph": {
                "nodes": [
                    {
                        "id": "fir",
                        "component": "orpheus.builtin.fir",
                        "params": {"channels": 1, "coefficients": {"$resource": "demo.values"}},
                    }
                ],
                "connections": [],
            },
        },
    )
    with pytest.raises(ValueError, match=message):
        ProjectLoader(builtin_registry()).load(project_dir)


def test_standalone_validation_accepts_directory_project_with_imports(tmp_path: Path) -> None:
    project_dir = tmp_path / "project"
    write_yaml(
        project_dir / "components" / "chain.component.yaml",
        composite_manifest("test.chain", "orpheus.builtin.gain", "gain"),
    )
    write_yaml(
        project_dir / "project.yaml",
        project_document(["components/chain.component.yaml"], "test.chain"),
    )
    assert discover_project_files([project_dir]) == [(project_dir / "project.yaml").resolve()]
    report = validate_project(project_dir / "project.yaml", builtin_registry())
    assert report.valid, [issue.message for issue in report.issues]


def test_imported_source_directory_is_forwarded_to_cmake(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    project_dir = tmp_path / "project"
    component_dir = project_dir / "native" / "local"
    write_yaml(component_dir / "component.yaml", atomic_manifest("test.local"))
    (component_dir / "CMakeLists.txt").write_text(
        "add_library(test_local SHARED src/local.c)\n", encoding="utf-8"
    )
    write_yaml(
        project_dir / "project.yaml",
        project_document(["native/local/component.yaml"], "test.local"),
    )
    project = ProjectLoader(builtin_registry()).load(project_dir)
    calls: list[list[str]] = []
    builder = ComponentBuilder(ROOT, tmp_path / "build", project.registry or builtin_registry())

    def fake_run(args: list[str]):
        calls.append(args)
        return subprocess.CompletedProcess(args, 0, "", "")

    monkeypatch.setattr(builder, "_run_cmake", fake_run)
    builder.configure_imported_components()
    external_arg = next(
        argument
        for argument in calls[0]
        if argument.startswith("-DORPHEUS_EXTRA_COMPONENT_DIRS=")
    )
    assert str(component_dir.resolve()) in external_arg
    assert str((ROOT / "components").resolve()) not in external_arg


def test_project_component_catalog_exposes_local_definition_and_readme(tmp_path: Path) -> None:
    project_dir = tmp_path / "workspace" / "demo"
    component_dir = project_dir / "components" / "local"
    write_yaml(component_dir / "component.yaml", atomic_manifest("test.local"))
    (component_dir / "README.md").write_text("# Local component\n", encoding="utf-8")
    write_yaml(
        project_dir / "project.yaml",
        {
            "version": "1.0.0",
            "imports": ["components/local/component.yaml"],
            "graph": {
                "nodes": [{"id": "local", "component": "test.local", "params": {}}],
                "connections": [],
            },
        },
    )
    with TestClient(create_app(tmp_path)) as client:
        response = client.get("/api/projects/demo/components")
        assert response.status_code == 200
        assert any(component["id"] == "test.local" for component in response.json())
        readme = client.get("/api/projects/demo/components/test.local/readme")
        assert readme.status_code == 200
        assert "Local component" in readme.text