"""In-memory project management with workspace persistence.

Projects live in memory as Project objects (ProjectManager._records) and are
written through to ``<project_root>/workspace/<name>/project.yaml`` on every
mutation. Components are NOT projects: they remain a global, read-only library
handled by the Registry.
"""

from __future__ import annotations

import os
import re
import shutil
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from orpheus_core import schemas
from orpheus_core.project import Graph, Project, ProjectLoader, Task, project_to_dict

_INVALID_PATH_CHARS = re.compile(r'[<>:"\\|?*\x00-\x1f]')
_RESERVED_PATH_NAME = re.compile(r"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)", re.I)
_IGNORED_DIRECTORIES = {"node_modules", "__pycache__"}


class ProjectError(Exception):
    """Base project error carrying an HTTP-ish status code."""

    status_code = 400

    def __init__(self, message: str):
        super().__init__(message)
        self.message = message


class ProjectNotFoundError(ProjectError):
    status_code = 404


class ProjectExistsError(ProjectError):
    status_code = 409


class ProjectValidationError(ProjectError):
    status_code = 400


@dataclass
class ProjectRecord:
    """A project loaded in memory."""

    name: str
    project: Project
    path: Path  # absolute path to project.yaml
    dirty: bool = False
    last_saved: float = field(default_factory=time.time)
    loaded_mtime_ns: int = 0

    @property
    def directory(self) -> Path:
        return self.path.parent


class ProjectManager:
    """Manages projects in memory, persisted under workspace/<name>/."""

    def __init__(self, project_root: Path, registry=None):
        self.project_root = Path(project_root).resolve()
        self.workspace = (self.project_root / "workspace").resolve()
        self.workspace.mkdir(parents=True, exist_ok=True)
        self._loader = ProjectLoader(registry)
        self._schema = schemas.load_project_schema()
        self._records: dict[str, ProjectRecord] = {}
        self._lock = threading.Lock()

    # ------------------------------------------------------------------ paths

    @staticmethod
    def _validate_name(name: str) -> None:
        parts = (name or "").split("/")
        if any(
            not part or part.startswith(".") or part.endswith((" ", "."))
            or part in _IGNORED_DIRECTORIES
            or _INVALID_PATH_CHARS.search(part) or _RESERVED_PATH_NAME.match(part)
            for part in parts
        ):
            raise ProjectValidationError(
                f"无效工程路径 {name!r}：请使用 workspace 内的相对路径，以 / 分隔目录，"
                "不可包含隐藏目录、上级目录、Windows 保留名称或非法文件名字符"
            )

    def project_dir(self, name: str) -> Path:
        self._validate_name(name)
        directory = self.workspace.joinpath(*name.split("/"))
        if directory.resolve() != directory:
            raise ProjectValidationError(f"工程路径不可经过符号链接或目录联接：{name}")
        return directory

    def _project_yaml(self, name: str) -> Path:
        path = self.project_dir(name) / "project.yaml"
        if path.is_symlink():
            raise ProjectValidationError(f"工程文件不可为符号链接：{name}")
        return path

    def project_paths(self) -> list[Path]:
        """递归发现工程；不跟随链接，不扫描版本库元数据和依赖缓存。"""
        def scan_error(error: OSError) -> None:
            raise ProjectValidationError(f"扫描 workspace 失败：{error}") from error

        paths = []
        for directory, children, files in os.walk(self.workspace, onerror=scan_error):
            parent = Path(directory)
            children[:] = sorted(
                child for child in children
                if not child.startswith(".") and child not in _IGNORED_DIRECTORIES
                and (parent / child).resolve() == parent / child
            )
            path = parent / "project.yaml"
            if (
                parent != self.workspace and "project.yaml" in files
                and path.is_file() and not path.is_symlink()
            ):
                paths.append(path)
        return sorted(paths)

    # ------------------------------------------------------------------ CRUD

    def list_projects(self) -> list[dict[str, Any]]:
        """List projects on disk, merged with in-memory dirty state."""
        result: list[dict[str, Any]] = []
        for yaml_path in self.project_paths():
            name = yaml_path.parent.relative_to(self.workspace).as_posix()
            metadata: dict[str, Any] = {}
            error = None
            try:
                with open(yaml_path, "r", encoding="utf-8") as f:
                    document = yaml.safe_load(f) or {}
                if not isinstance(document, dict):
                    raise ValueError("工程文档必须为映射")
                metadata = document.get("metadata", {}) or {}
                if not isinstance(metadata, dict):
                    raise ValueError("metadata 必须为映射")
            except (OSError, UnicodeError, yaml.YAMLError, ValueError) as exc:
                error = f"读取工程信息失败：{exc}"
                metadata = {}
            rec = self._records.get(name)
            item = {
                "name": name,
                "metadata": metadata,
                "mtime": yaml_path.stat().st_mtime,
                "dirty": rec.dirty if rec else False,
                "open": rec is not None,
            }
            if error:
                item["error"] = error
            result.append(item)
        return sorted(result, key=lambda item: item["name"])

    def list_examples(self) -> list[str]:
        """List flat YAML and directory examples under <root>/examples/."""
        examples = self.project_root / "examples"
        if not examples.exists():
            return []
        names = {path.stem for path in examples.glob("*.yaml")}
        names.update(
            child.name
            for child in examples.iterdir()
            if child.is_dir() and (child / "project.yaml").is_file()
        )
        return sorted(names)

    def create(self, name: str, from_example: str | None = None) -> ProjectRecord:
        with self._lock:
            pdir = self.project_dir(name)
            yaml_path = self._project_yaml(name)
            if yaml_path.exists():
                raise ProjectExistsError(f"project already exists: {name}")
            pdir.mkdir(parents=True, exist_ok=True)

            if from_example:
                project = self._import_example(from_example, pdir)
                project.metadata["name"] = name
            else:
                project = Project(
                    metadata={"name": name, "description": ""},
                    sample_rate=48000,
                    block_size=128,
                    buffer_size=0,
                )
                project.tasks["default"] = Task(id="default", name="Default")
                project.graph = Graph()

            self._loader.save(project, yaml_path)
            rec = ProjectRecord(
                name=name,
                project=project,
                path=yaml_path,
                loaded_mtime_ns=yaml_path.stat().st_mtime_ns,
            )
            self._records[name] = rec
            return rec

    def _import_example(self, example: str, pdir: Path) -> Project:
        example_root = self.project_root / "examples" / example
        if (example_root / "project.yaml").is_file():
            shutil.copytree(example_root, pdir, dirs_exist_ok=True)
            for generated in pdir.glob("*.plan.json"):
                generated.unlink()
            return self._loader.load(pdir / "project.yaml")

        src = self.project_root / "examples" / f"{example}.yaml"
        if not src.exists():
            raise ProjectNotFoundError(f"example not found: {example}")
        project = self._loader.load(src)
        # Copy optional documentation sidecars from the example directory.
        for sidecar_name in (f"{example}.notes.md", f"{example}.node-notes.json"):
            sidecar_src = self.project_root / "examples" / sidecar_name
            if sidecar_src.is_file():
                shutil.copy2(sidecar_src, pdir / sidecar_name.replace(f"{example}.", ""))
        # Rewrite file_path params to project-relative paths. A node with an
        # incoming connection but no outgoing one is a sink: its file is an
        # output and goes to outputs/. Other existing files are inputs and get
        # copied into the project dir.
        incoming = {c.to_ref.node_id for c in project.graph.connections}
        outgoing = {c.from_ref.node_id for c in project.graph.connections}
        for node in project.graph.nodes.values():
            file_path = node.params.get("file_path")
            if not isinstance(file_path, str) or not file_path:
                continue
            p = Path(file_path)
            is_sink = node.id in incoming and node.id not in outgoing
            # 源文件解析：绝对路径直接用；相对路径相对示例目录解析（示例可移植）。
            src = p if p.is_absolute() else src.parent / p
            if is_sink or not src.is_file():
                # 输出文件或源文件不存在 → 落到 outputs/（保持历史行为）
                node.params["file_path"] = f"outputs/{p.name}"
                continue
            dest = pdir / p.name
            if dest.resolve() != src.resolve():
                shutil.copy2(src, dest)
            node.params["file_path"] = p.name
        return project

    def get(self, name: str) -> ProjectRecord:
        """Get a project record, loading from disk on first access."""
        with self._lock:
            yaml_path = self._project_yaml(name)
            rec = self._records.get(name)
            if rec is not None:
                current_mtime_ns = rec.path.stat().st_mtime_ns if rec.path.is_file() else 0
                if not rec.dirty and current_mtime_ns != rec.loaded_mtime_ns:
                    rec = ProjectRecord(
                        name=name,
                        project=self._loader.load(rec.path),
                        path=rec.path,
                        loaded_mtime_ns=current_mtime_ns,
                    )
                    self._records[name] = rec
                return rec
            if not yaml_path.is_file():
                raise ProjectNotFoundError(f"project not found: {name}")
            project = self._loader.load(yaml_path)
            rec = ProjectRecord(
                name=name,
                project=project,
                path=yaml_path,
                loaded_mtime_ns=yaml_path.stat().st_mtime_ns,
            )
            self._records[name] = rec
            return rec

    def invalidate_cached_projects(self) -> int:
        """Drop loaded projects so Registry forks rebuild on next access.

        Project mutations are persisted synchronously, so records are only a
        cache. A global component rescan otherwise leaves each loaded project
        pointing at the old Registry snapshot.
        """
        with self._lock:
            count = len(self._records)
            self._records.clear()
            return count

    def get_document(self, name: str) -> dict[str, Any]:
        """Return the project as a plain JSON-serializable document."""
        return project_to_dict(self.get(name).project, materialize_imports=True)

    @staticmethod
    def _merge_composite_manifest(existing: dict[str, Any], sub: dict[str, Any]) -> dict[str, Any]:
        manifest = dict(existing)
        manifest.update({
            "id": sub["id"],
            "name": sub.get("name", sub["id"]),
            "description": sub.get("description", ""),
            "version": manifest.get("version", "1.0.0"),
            "abi_version": manifest.get("abi_version", 1),
            "package_type": "composite",
            "category": manifest.get("category", "工程/复合组件"),
            "graph": sub.get("graph", {"nodes": [], "connections": []}),
        })
        old_ports = {port["id"]: port for port in manifest.get("ports", []) or []}
        ports = []
        for port in sub.get("ports", []) or []:
            merged = dict(old_ports.get(port["id"], {}))
            merged.update(port)
            merged.setdefault("type", "audio")
            merged.setdefault("sample_format", "f32")
            ports.append(merged)
        manifest["ports"] = ports
        old_parameters = {
            parameter["id"]: parameter
            for parameter in manifest.get("parameters", []) or []
        }
        parameters = []
        for parameter in sub.get("public_parameters", []) or []:
            merged = dict(old_parameters.get(parameter["id"], {}))
            merged.update(parameter)
            merged.setdefault("type", "float")
            parameters.append(merged)
        manifest["parameters"] = parameters
        manifest.pop("public_ports", None)
        manifest.pop("public_parameters", None)
        return manifest

    def _persist_composite_definitions(
        self,
        name: str,
        document: dict[str, Any],
        current: Project,
    ) -> dict[str, Any]:
        """Write materialized composite definitions back to their own files."""
        root_document = dict(document)
        imports = list(root_document.get("imports", []) or [])
        existing_paths = {
            sub.id: sub.source_path
            for sub in current.subcomponents
            if sub.imported and not sub.read_only and sub.source_path is not None
        }
        inline: list[dict[str, Any]] = []
        for sub in root_document.get("subcomponents", []) or []:
            component_id = sub.get("id", "")
            if sub.get("read_only"):
                continue
            source_path = existing_paths.get(component_id)
            if source_path is None:
                if "." not in component_id:
                    inline.append(sub)
                    continue
                source_path = self.project_dir(name) / "components" / f"{component_id}.component.yaml"
                relative = source_path.relative_to(self.project_dir(name)).as_posix()
                if relative not in imports:
                    imports.append(relative)
            existing: dict[str, Any] = {}
            if source_path.is_file():
                with open(source_path, "r", encoding="utf-8") as stream:
                    existing = yaml.safe_load(stream) or {}
            manifest = self._merge_composite_manifest(existing, sub)
            schemas.validate(manifest, schemas.load_component_manifest_schema())
            source_path.parent.mkdir(parents=True, exist_ok=True)
            with open(source_path, "w", encoding="utf-8", newline="\n") as stream:
                yaml.safe_dump(manifest, stream, sort_keys=False, allow_unicode=True)
        if imports:
            root_document["imports"] = imports
        else:
            root_document.pop("imports", None)
        if inline:
            root_document["subcomponents"] = inline
        else:
            root_document.pop("subcomponents", None)
        return root_document

    def put(self, name: str, doc: dict[str, Any]) -> ProjectRecord:
        """Validate and persist a full project document (write-through)."""
        with self._lock:
            yaml_path = self._project_yaml(name)
            if not yaml_path.is_file():
                raise ProjectNotFoundError(f"project not found: {name}")
            try:
                schemas.validate(doc, self._schema)
            except Exception as exc:
                raise ProjectValidationError(f"invalid project document: {exc}") from exc
            current_record = self._records.get(name)
            current = (
                current_record.project
                if current_record is not None
                else self._loader.load(yaml_path)
            )
            persistent_doc = self._persist_composite_definitions(name, doc, current)
            with open(yaml_path, "w", encoding="utf-8", newline="\n") as f:
                yaml.safe_dump(persistent_doc, f, sort_keys=False, allow_unicode=True)
            try:
                project = self._loader.load(yaml_path)
            except Exception as exc:
                raise ProjectValidationError(f"invalid project document: {exc}") from exc
            rec = ProjectRecord(
                name=name,
                project=project,
                path=yaml_path,
                loaded_mtime_ns=yaml_path.stat().st_mtime_ns,
            )
            saved_definitions = {
                sub.source_path for sub in project.subcomponents
                if sub.imported and not sub.read_only and sub.source_path is not None
            }
            # 一个定义可被父、子工程共同导入；保存后不能继续返回另一工程的旧快照。
            for cached_name, cached in list(self._records.items()):
                if cached_name != name and any(
                    sub.source_path in saved_definitions
                    for sub in cached.project.subcomponents
                ):
                    self._records.pop(cached_name)
            self._records[name] = rec
            return rec

    def delete(self, name: str) -> None:
        with self._lock:
            pdir = self.project_dir(name)
            if not self._project_yaml(name).is_file():
                raise ProjectNotFoundError(f"project not found: {name}")
            nested = [
                path.parent.relative_to(self.workspace).as_posix()
                for path in self.project_paths()
                if path.parent != pdir and pdir in path.parents
            ]
            if nested:
                raise ProjectValidationError(
                    f"工程目录包含子工程，请先移动或删除子工程：{', '.join(nested)}"
                )
            self._records.pop(name, None)
            shutil.rmtree(pdir)
