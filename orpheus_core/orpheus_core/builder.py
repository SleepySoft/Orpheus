"""Build orchestrator: compile components into dynamic libraries."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path
from typing import Any

from orpheus_core.registry import ComponentInfo, Registry


class BuildError(Exception):
    pass


class ComponentBuilder:
    def __init__(
        self,
        project_root: Path,
        build_dir: Path,
        registry: Registry,
        cmake_generator: str = "Ninja",
    ):
        self.project_root = Path(project_root)
        self.build_dir = Path(build_dir)
        self.registry = registry
        self.cmake_generator = cmake_generator

    def _cmake_target_name(self, component_id: str) -> str:
        # orpheus.builtin.gain -> orpheus_builtin_gain
        return component_id.replace(".", "_")

    def _library_path(self, component_id: str) -> Path | None:
        target = self._cmake_target_name(component_id)
        candidates = [
            self.build_dir / "components" / f"{target}.dll",
            self.build_dir / "components" / f"lib{target}.dll",
            self.build_dir / "components" / f"lib{target}.so",
            self.build_dir / "components" / f"lib{target}.dylib",
        ]
        for c in candidates:
            if c.exists():
                return c
        return None

    def configure(self, extra_cmake_args: list[str] | None = None) -> None:
        self.build_dir.mkdir(parents=True, exist_ok=True)
        args = [
            "cmake",
            "-S", str(self.project_root),
            "-B", str(self.build_dir),
            "-G", self.cmake_generator,
            "-DORPHEUS_BUILD_RUNTIME=ON",
            "-DORPHEUS_BUILD_COMPONENTS=ON",
            "-DORPHEUS_BUILD_TESTS=OFF",
        ]
        if extra_cmake_args:
            args.extend(extra_cmake_args)
        env = os.environ.copy()
        result = subprocess.run(args, cwd=self.project_root, env=env, capture_output=True, text=True)
        if result.returncode != 0:
            raise BuildError(f"cmake configure failed:\n{result.stderr}\n{result.stdout}")

    def build_component(self, component_id: str) -> Path:
        info = self.registry.get(component_id)
        if info is None:
            raise BuildError(f"component not in registry: {component_id}")
        if info.package_type == "binary":
            # Binary package: verify artifact exists
            artifact = self._resolve_binary_artifact(info)
            if artifact is None or not artifact.exists():
                raise BuildError(f"binary artifact missing for {component_id}")
            return artifact

        target = self._cmake_target_name(component_id)
        args = ["cmake", "--build", str(self.build_dir), "--target", target]
        env = os.environ.copy()
        result = subprocess.run(args, cwd=self.project_root, env=env, capture_output=True, text=True)
        if result.returncode != 0:
            raise BuildError(f"build failed for {component_id}:\n{result.stderr}\n{result.stdout}")

        lib_path = self._library_path(component_id)
        if lib_path is None:
            raise BuildError(f"library not found after building {component_id}")
        return lib_path

    def _resolve_binary_artifact(self, info: ComponentInfo) -> Path | None:
        binaries = info.manifest.get("binaries", [])
        # 简化：取第一个存在的 artifact
        for b in binaries:
            p = info.root_dir / b["artifact"]
            if p.exists():
                return p
        return None
