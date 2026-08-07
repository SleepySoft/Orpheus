"""In-memory project management with workspace persistence.

Projects live in memory as Project objects (ProjectManager._records) and are
written through to ``<project_root>/workspace/<name>/project.yaml`` on every
mutation. Components are NOT projects: they remain a global, read-only library
handled by the Registry.
"""

from __future__ import annotations

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

_NAME_RE = re.compile(r"^[A-Za-z0-9_-]+$")


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

    @property
    def directory(self) -> Path:
        return self.path.parent


class ProjectManager:
    """Manages projects in memory, persisted under workspace/<name>/."""

    def __init__(self, project_root: Path):
        self.project_root = Path(project_root).resolve()
        self.workspace = self.project_root / "workspace"
        self.workspace.mkdir(parents=True, exist_ok=True)
        self._loader = ProjectLoader()
        self._schema = schemas.load_project_schema()
        self._records: dict[str, ProjectRecord] = {}
        self._lock = threading.Lock()

    # ------------------------------------------------------------------ paths

    @staticmethod
    def _validate_name(name: str) -> None:
        if not _NAME_RE.match(name or ""):
            raise ProjectValidationError(
                f"invalid project name {name!r}: use letters, digits, '-' and '_' only"
            )

    def project_dir(self, name: str) -> Path:
        self._validate_name(name)
        return self.workspace / name

    def _project_yaml(self, name: str) -> Path:
        return self.project_dir(name) / "project.yaml"

    # ------------------------------------------------------------------ CRUD

    def list_projects(self) -> list[dict[str, Any]]:
        """List projects on disk, merged with in-memory dirty state."""
        result: list[dict[str, Any]] = []
        for child in sorted(self.workspace.iterdir()):
            yaml_path = child / "project.yaml"
            if not child.is_dir() or not yaml_path.exists():
                continue
            metadata: dict[str, Any] = {}
            try:
                with open(yaml_path, "r", encoding="utf-8") as f:
                    metadata = (yaml.safe_load(f) or {}).get("metadata", {}) or {}
            except Exception:
                pass
            rec = self._records.get(child.name)
            result.append(
                {
                    "name": child.name,
                    "metadata": metadata,
                    "mtime": yaml_path.stat().st_mtime,
                    "dirty": rec.dirty if rec else False,
                    "open": rec is not None,
                }
            )
        return result

    def list_examples(self) -> list[str]:
        """List importable example projects from <root>/examples/*.yaml."""
        examples = self.project_root / "examples"
        if not examples.exists():
            return []
        return sorted(p.stem for p in examples.glob("*.yaml"))

    def create(self, name: str, from_example: str | None = None) -> ProjectRecord:
        with self._lock:
            pdir = self.project_dir(name)
            yaml_path = pdir / "project.yaml"
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
            rec = ProjectRecord(name=name, project=project, path=yaml_path)
            self._records[name] = rec
            return rec

    def _import_example(self, example: str, pdir: Path) -> Project:
        src = self.project_root / "examples" / f"{example}.yaml"
        if not src.exists():
            raise ProjectNotFoundError(f"example not found: {example}")
        project = self._loader.load(src)
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
            rec = self._records.get(name)
            if rec is not None:
                return rec
            yaml_path = self._project_yaml(name)
            if not yaml_path.exists():
                raise ProjectNotFoundError(f"project not found: {name}")
            project = self._loader.load(yaml_path)
            rec = ProjectRecord(name=name, project=project, path=yaml_path)
            self._records[name] = rec
            return rec

    def get_document(self, name: str) -> dict[str, Any]:
        """Return the project as a plain JSON-serializable document."""
        return project_to_dict(self.get(name).project)

    def put(self, name: str, doc: dict[str, Any]) -> ProjectRecord:
        """Validate and persist a full project document (write-through)."""
        with self._lock:
            yaml_path = self._project_yaml(name)
            if not yaml_path.exists():
                raise ProjectNotFoundError(f"project not found: {name}")
            try:
                schemas.validate(doc, self._schema)
            except Exception as exc:
                raise ProjectValidationError(f"invalid project document: {exc}") from exc
            with open(yaml_path, "w", encoding="utf-8") as f:
                yaml.safe_dump(doc, f, sort_keys=False, allow_unicode=True)
            try:
                project = self._loader.load(yaml_path)
            except Exception as exc:
                raise ProjectValidationError(f"invalid project document: {exc}") from exc
            rec = ProjectRecord(name=name, project=project, path=yaml_path)
            self._records[name] = rec
            return rec

    def delete(self, name: str) -> None:
        with self._lock:
            pdir = self.project_dir(name)
            if not (pdir / "project.yaml").exists():
                raise ProjectNotFoundError(f"project not found: {name}")
            self._records.pop(name, None)
            shutil.rmtree(pdir)
