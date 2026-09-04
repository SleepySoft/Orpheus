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


def _find_vcvars64() -> str | None:
    """Locate vcvars64.bat for the latest VS install, or None."""
    candidates: list[Path] = []
    if os.environ.get("VSINSTALLDIR"):
        candidates.append(Path(os.environ["VSINSTALLDIR"]) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat")
    vswhere = (
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    )
    if vswhere.exists():
        try:
            out = subprocess.run(
                [
                    str(vswhere),
                    "-latest",
                    "-products",
                    "*",
                    "-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-property",
                    "installationPath",
                ],
                capture_output=True,
                text=True,
                timeout=15,
            )
            install = out.stdout.strip()
            if install:
                candidates.append(Path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat")
        except (OSError, subprocess.SubprocessError):
            pass
    for p in candidates:
        if p.exists():
            return str(p)
    return None


def configured_c_compiler(build_dir: Path) -> str | None:
    """Return the configured C compiler path from CMakeCache, if available."""
    cache = Path(build_dir) / "CMakeCache.txt"
    if not cache.exists():
        return None
    try:
        for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith("CMAKE_C_COMPILER:") and "=" in line:
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None


def _configured_compiler_is_msvc(build_dir: Path) -> bool:
    """Check whether the configured C compiler is MSVC (cl.exe)."""
    compiler = configured_c_compiler(build_dir)
    return bool(compiler and ("MSVC" in compiler or "cl.exe" in compiler.lower()))


def run_cmake_with_msvc_env(
    args: list[str], cwd: Path, build_dir: Path
) -> subprocess.CompletedProcess[str]:
    """Run a cmake command, loading the MSVC environment first when needed.

    Ninja + MSVC requires INCLUDE/LIB environment variables (from vcvars64.bat).
    When the configured compiler is MSVC, wrap the command via cmd so plain
    shells work too; MinGW/gcc builds are unaffected.
    """
    env = os.environ.copy()
    cache = Path(build_dir) / "CMakeCache.txt"
    command: str | list[str] = args
    shell = False
    if _configured_compiler_is_msvc(build_dir) or not cache.exists():
        vcvars = _find_vcvars64()
        if vcvars:
            quoted = " ".join(f'"{a}"' for a in args)
            command = f'call "{vcvars}" >nul && {quoted}'
            shell = True
    result = subprocess.run(
        command, cwd=cwd, env=env, shell=shell, capture_output=True, text=True,
        encoding="utf-8", errors="replace",
    )
    output = (result.stdout or "") + (result.stderr or "")
    transient_archive_lock = (
        os.name == "nt"
        and len(args) >= 2
        and args[0] == "cmake"
        and args[1] == "--build"
        and "-j1" not in args
        and "could not create temporary file whilst writing archive: Permission denied" in output
    )
    if result.returncode != 0 and transient_archive_lock:
        retry_args = [*args, "--", "-j1"]
        return run_cmake_with_msvc_env(retry_args, cwd, build_dir)
    return result


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

    def _run_cmake(self, args: list[str]) -> subprocess.CompletedProcess[str]:
        return run_cmake_with_msvc_env(args, self.project_root, self.build_dir)

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

    def find_library(self, component_id: str) -> Path | None:
        """Return the built library path for a component, or None if not built."""
        return self._library_path(component_id)

    def configure(self, extra_cmake_args: list[str] | None = None) -> None:
        self.build_dir.mkdir(parents=True, exist_ok=True)
        args = [
            "cmake",
            "-S", str(self.project_root),
            "-B", str(self.build_dir),
            "-G", self.cmake_generator,
            "-DORPHEUS_BUILD_RUNTIME=ON",
            "-DORPHEUS_BUILD_COMPONENTS=ON",
            "-DORPHEUS_BUILD_TESTS=ON",
        ]
        if extra_cmake_args:
            args.extend(extra_cmake_args)
        result = self._run_cmake(args)
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
        result = self._run_cmake(args)
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
