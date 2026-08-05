"""FastAPI application exposing Orpheus core services to the UI."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from fastapi import Body, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel

from orpheus_core.builder import BuildError, ComponentBuilder
from orpheus_core.compiler import CompileError, ExecutionPlan, GraphCompiler
from orpheus_core.registry import ComponentInfo, Registry
from orpheus_core.server.manager import ProjectError, ProjectManager, ProjectRecord
from orpheus_core.subgraph import flatten_project

RUN_TIMEOUT_SECONDS = 60


class CreateProjectRequest(BaseModel):
    name: str
    from_example: str | None = None


def _component_to_dict(info: ComponentInfo) -> dict[str, Any]:
    m = info.manifest
    return {
        "id": info.id,
        "version": info.version,
        "abi_version": info.abi_version,
        "package_type": info.package_type,
        "name": m.get("name", info.id),
        "category": m.get("category", "未分类"),
        "description": m.get("description", ""),
        "ports": m.get("ports", []),
        "parameters": m.get("parameters", []),
    }


def _runtime_exe_name() -> str:
    return "orpheus_runtime.exe" if sys.platform == "win32" else "orpheus_runtime"


def create_app(project_root: Path) -> FastAPI:
    root = Path(project_root).resolve()
    app = FastAPI(title="Orpheus Server", version="0.1.0")
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["http://localhost:3000", "http://127.0.0.1:3000"],
        allow_methods=["*"],
        allow_headers=["*"],
    )

    registry = Registry()
    registry.add_search_path(root / "components")
    registry.scan()
    manager = ProjectManager(root)
    builder = ComponentBuilder(root, root / "build", registry)
    state: dict[str, Any] = {"cmake_configured": (root / "build" / "CMakeCache.txt").exists()}

    # ------------------------------------------------------------- helpers

    def compile_record(rec: ProjectRecord) -> tuple[ExecutionPlan, Path]:
        """Flatten subcomponents, compile to a plan, and write plan.json."""
        try:
            flat = flatten_project(rec.project)
            plan = GraphCompiler(registry).compile(flat)
        except CompileError as exc:
            raise HTTPException(status_code=400, detail=f"compile error: {exc}") from exc
        plan_path = rec.path.with_suffix(".plan.json")
        with open(plan_path, "w", encoding="utf-8") as f:
            json.dump(plan.__dict__, f, indent=2, ensure_ascii=False)
        rec.dirty = False
        return plan, plan_path

    def flattened_project(rec: ProjectRecord):
        try:
            return flatten_project(rec.project)
        except CompileError as exc:
            raise HTTPException(status_code=400, detail=f"compile error: {exc}") from exc

    def ensure_cmake_configured() -> None:
        if state["cmake_configured"]:
            return
        try:
            builder.configure()
        except BuildError as exc:
            raise HTTPException(status_code=500, detail=f"cmake configure error: {exc}") from exc
        state["cmake_configured"] = True

    def ensure_components_built(flat) -> list[str]:
        """Build every atomic component referenced by the flattened graph."""
        built: list[str] = []
        component_ids = {n.component for n in flat.graph.nodes.values()}
        for cid in sorted(component_ids):
            if builder.find_library(cid) is not None:
                continue
            ensure_cmake_configured()
            try:
                builder.build_component(cid)
            except BuildError as exc:
                raise HTTPException(status_code=500, detail=f"build error for {cid}: {exc}") from exc
            built.append(cid)
        return built

    def ensure_runtime_built() -> Path:
        exe = root / "build" / _runtime_exe_name()
        if exe.exists():
            return exe
        ensure_cmake_configured()
        result = subprocess.run(
            ["cmake", "--build", str(root / "build"), "--target", "orpheus_runtime"],
            cwd=root, capture_output=True, text=True,
        )
        if result.returncode != 0 or not exe.exists():
            raise HTTPException(
                status_code=500,
                detail=f"failed to build orpheus_runtime:\n{result.stderr}\n{result.stdout}",
            )
        return exe

    def safe_project_file(name: str, relpath: str) -> Path:
        pdir = manager.project_dir(name).resolve()
        target = (pdir / relpath).resolve()
        if not target.is_relative_to(pdir):
            raise HTTPException(status_code=400, detail="invalid file path")
        if not target.is_file():
            raise HTTPException(status_code=404, detail=f"file not found: {relpath}")
        return target

    # ------------------------------------------------------------- routes

    @app.get("/api/health")
    def health() -> dict[str, Any]:
        return {"status": "ok", "project_root": str(root)}

    # ---- components (global read-only library)

    @app.get("/api/components")
    def list_components() -> list[dict[str, Any]]:
        return [_component_to_dict(i) for i in registry.list_components()]

    @app.post("/api/components/rescan")
    def rescan_components() -> dict[str, Any]:
        registry.scan()
        return {"count": len(registry.list_components())}

    # ---- projects (user documents in workspace/)

    @app.get("/api/projects")
    def list_projects() -> list[dict[str, Any]]:
        return manager.list_projects()

    @app.get("/api/examples")
    def list_examples() -> list[str]:
        return manager.list_examples()

    @app.post("/api/projects", status_code=201)
    def create_project(req: CreateProjectRequest) -> dict[str, Any]:
        try:
            rec = manager.create(req.name, from_example=req.from_example)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        return {"name": rec.name, "document": manager.get_document(rec.name)}

    @app.get("/api/projects/{name}")
    def get_project(name: str) -> dict[str, Any]:
        try:
            return manager.get_document(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc

    @app.put("/api/projects/{name}")
    def put_project(name: str, doc: dict[str, Any] = Body(...)) -> dict[str, Any]:
        try:
            manager.put(name, doc)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        return {"status": "saved", "name": name}

    @app.delete("/api/projects/{name}")
    def delete_project(name: str) -> dict[str, Any]:
        try:
            manager.delete(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        return {"status": "deleted", "name": name}

    @app.post("/api/projects/{name}/compile")
    def compile_project(name: str) -> dict[str, Any]:
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        plan, plan_path = compile_record(rec)
        return {
            "status": "ok",
            "plan_path": str(plan_path),
            "nodes": len(plan.nodes),
            "execution_order": plan.execution_order,
            "buffers": len(plan.buffers),
            "connections": len(plan.connections),
        }

    @app.post("/api/projects/{name}/run")
    def run_project(name: str) -> dict[str, Any]:
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        built = ensure_components_built(flattened_project(rec))
        _, plan_path = compile_record(rec)
        exe = ensure_runtime_built()

        project_dir = rec.directory
        (project_dir / "outputs").mkdir(exist_ok=True)
        try:
            result = subprocess.run(
                [str(exe), str(plan_path), str(root / "build" / "components")],
                cwd=project_dir, capture_output=True, text=True,
                timeout=RUN_TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired as exc:
            raise HTTPException(
                status_code=500,
                detail=f"runtime timed out after {RUN_TIMEOUT_SECONDS}s",
            ) from exc

        outputs = sorted(
            p.relative_to(project_dir).as_posix()
            for p in (project_dir / "outputs").rglob("*")
            if p.is_file()
        )
        return {
            "status": "ok" if result.returncode == 0 else "error",
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "built_components": built,
            "outputs": outputs,
        }

    @app.get("/api/projects/{name}/files/{relpath:path}")
    def get_project_file(name: str, relpath: str) -> FileResponse:
        try:
            manager.project_dir(name)  # validates name
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        return FileResponse(safe_project_file(name, relpath))

    @app.get("/api/projects/{name}/download")
    def download_project(name: str) -> FileResponse:
        try:
            pdir = manager.project_dir(name)
            if not (pdir / "project.yaml").exists():
                raise HTTPException(status_code=404, detail=f"project not found: {name}")
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        archive = shutil.make_archive(
            str(Path(tempfile.gettempdir()) / f"orpheus_{name}"), "zip", root_dir=pdir
        )
        return FileResponse(archive, filename=f"{name}.zip")

    # ---- static UI hosting (single-command mode: API + web on one port)
    ui_build = root / "ui" / "build"
    if (ui_build / "index.html").exists():
        from fastapi.staticfiles import StaticFiles

        app.mount("/", StaticFiles(directory=ui_build, html=True), name="ui")
    else:
        @app.get("/")
        def index_hint() -> dict[str, Any]:
            return {
                "hint": "UI build not found. Run `cd ui && npm run build` for single-command "
                        "mode, or `npm start` for frontend development (API is under /api)."
            }

    return app
