"""Component registry: scan, load and query component manifests."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from orpheus_core import schemas


@dataclass
class ComponentInfo:
    id: str
    version: str
    abi_version: int
    package_type: str
    manifest_path: Path
    root_dir: Path
    manifest: dict[str, Any] = field(repr=False)


class Registry:
    def __init__(self, search_paths: list[Path] | None = None):
        self.search_paths = search_paths or []
        self._components: dict[str, ComponentInfo] = {}
        self._schema = schemas.load_component_manifest_schema()

    def add_search_path(self, path: Path) -> None:
        self.search_paths.append(Path(path))

    def scan(self) -> dict[str, ComponentInfo]:
        """Scan all search paths and return discovered components by id."""
        self._components = {}
        for base in self.search_paths:
            if not base.exists():
                continue
            for manifest_path in base.rglob("component.yaml"):
                try:
                    info = self._load_manifest(manifest_path)
                except Exception as exc:
                    print(f"[Registry] skip {manifest_path}: {exc}")
                    continue
                # 多个版本时保留最新（语义化版本简单字符串比较）
                existing = self._components.get(info.id)
                if existing is None or info.version > existing.version:
                    self._components[info.id] = info
        return dict(self._components)

    def _load_manifest(self, manifest_path: Path) -> ComponentInfo:
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = yaml.safe_load(f)
        if not isinstance(manifest, dict):
            raise ValueError("manifest is not a mapping")
        schemas.validate(manifest, self._schema)

        comp_id = manifest["id"]
        version = manifest.get("version", "0.0.0")
        abi_version = manifest.get("abi_version", 1)
        package_type = manifest.get("package_type", "source")

        return ComponentInfo(
            id=comp_id,
            version=version,
            abi_version=abi_version,
            package_type=package_type,
            manifest_path=manifest_path,
            root_dir=manifest_path.parent,
            manifest=manifest,
        )

    def get(self, component_id: str) -> ComponentInfo | None:
        return self._components.get(component_id)

    def list_components(self) -> list[ComponentInfo]:
        return list(self._components.values())

    def source_files(self, info: ComponentInfo) -> list[Path]:
        """Return absolute source file paths for a source package."""
        sources = info.manifest.get("sources", [])
        return [info.root_dir / s for s in sources]

    def include_dirs(self, info: ComponentInfo) -> list[Path]:
        """Return include directories for a source package."""
        headers = info.manifest.get("headers", [])
        dirs: set[Path] = set()
        for h in headers:
            p = info.root_dir / h
            if p.exists() and p.is_file():
                dirs.add(p.parent)
        # 如果组件没声明头文件目录，默认包含 include/
        default_include = info.root_dir / "include"
        if default_include.exists():
            dirs.add(default_include)
        return list(dirs)
