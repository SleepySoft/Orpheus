"""FastAPI application exposing Orpheus core services to the UI."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

import time

from fastapi import Body, FastAPI, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel

from orpheus_core.builder import BuildError, ComponentBuilder, run_cmake_with_msvc_env
from orpheus_core.compiler import CompileError, ExecutionPlan, GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.registry import ComponentInfo, Registry
from orpheus_core.server.manager import ProjectError, ProjectManager, ProjectRecord
from orpheus_core.server.rt import RtSessionManager
from orpheus_core.subgraph import flatten_project

RUN_TIMEOUT_SECONDS = 60
DEVICES_CACHE_SECONDS = 30
DEVICE_COMPONENTS = {"orpheus.builtin.device_in", "orpheus.builtin.device_out"}


def _wav_total_frames(path: Path) -> int:
    try:
        import wave

        with wave.open(str(path), "rb") as w:
            return w.getnframes()
    except Exception:
        return 0


def _parse_probe_lines(stdout: str) -> list[dict[str, Any]]:
    """Parse probe lines printed by the offline host.

    - `PROBE <node> <param> <value>`: scalar value (float or raw string)
    - `PROBE_JSON <node> <param> <json>`: structured value (array/object/number)
    """
    probes = []
    for line in stdout.splitlines():
        parts = line.split(maxsplit=3)
        if len(parts) == 4 and parts[0] == "PROBE":
            try:
                value: Any = float(parts[3])
            except ValueError:
                value = parts[3]
            probes.append({"node": parts[1], "param": parts[2], "value": value})
        elif len(parts) == 4 and parts[0] == "PROBE_JSON":
            try:
                value = json.loads(parts[3])
            except (json.JSONDecodeError, ValueError):
                value = parts[3]
            probes.append({"node": parts[1], "param": parts[2], "value": value})
    return probes


class CreateProjectRequest(BaseModel):
    name: str
    from_example: str | None = None


class RtParamRequest(BaseModel):
    node: str
    param: str
    value: float | int | str


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
        "clock_source": m.get("clock_source", False),
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
    rt_sessions = RtSessionManager()
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

    def ensure_target_built(target: str, exe_name: str) -> Path:
        exe = root / "build" / exe_name
        if exe.exists():
            return exe
        ensure_cmake_configured()
        result = run_cmake_with_msvc_env(
            ["cmake", "--build", str(root / "build"), "--target", target],
            cwd=root, build_dir=root / "build",
        )
        if result.returncode != 0 or not exe.exists():
            raise HTTPException(
                status_code=500,
                detail=f"failed to build {target}:\n{result.stderr}\n{result.stdout}",
            )
        return exe

    def ensure_runtime_built() -> Path:
        return ensure_target_built("orpheus_runtime", _runtime_exe_name())

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

    @app.get("/api/devices")
    def list_devices() -> dict[str, Any]:
        """Enumerate audio devices via rt_host --list-devices (cached briefly)."""
        cached = state.get("devices_cache")
        if cached and time.time() - cached[0] < DEVICES_CACHE_SECONDS:
            return cached[1]
        exe = root / "build" / ("orpheus_rt_host.exe" if sys.platform == "win32" else "orpheus_rt_host")
        if not exe.exists():
            raise HTTPException(status_code=400, detail="rt_host not built; run `orpheus-cli build` first")
        try:
            result = subprocess.run(
                [str(exe), "--list-devices"], capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=15
            )
        except subprocess.TimeoutExpired as exc:
            raise HTTPException(status_code=500, detail="device enumeration timed out") from exc
        if result.returncode != 0:
            raise HTTPException(status_code=500, detail=f"device enumeration failed: {result.stderr}")
        try:
            devices = json.loads(result.stdout.strip())
        except json.JSONDecodeError as exc:
            raise HTTPException(status_code=500, detail=f"invalid device list: {result.stdout[:200]}") from exc
        state["devices_cache"] = (time.time(), devices)
        return devices

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
            # per-node rate info for UI badges (time-tree visualization)
            "node_rates": {
                nid: {
                    "divisor": cfg.get("divisor", 1),
                    "sample_rate": cfg.get("sample_rate"),
                    "frames": cfg.get("frames"),
                }
                for nid, cfg in plan.node_configs.items()
            },
        }

    @app.post("/api/projects/{name}/run")
    def run_project(name: str, pace: bool = False) -> dict[str, Any]:
        """Base-host run: dispatch by graph IO. Graphs with device components run
        as a realtime session; pure file graphs run the offline host.
        pace=true：离线图按真实时长播放（会话方式运行，探针流式上报）。"""
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        flat = flattened_project(rec)
        has_device = any(n.component in DEVICE_COMPONENTS for n in flat.graph.nodes.values())

        built = ensure_components_built(flat)
        plan, plan_path = compile_record(rec)

        if has_device:
            suffix = ".exe" if sys.platform == "win32" else ""
            rt_exe = ensure_target_built("orpheus_rt_host", f"orpheus_rt_host{suffix}")
            try:
                session = rt_sessions.start(
                    name,
                    [str(rt_exe), str(plan_path), str(root / "build" / "components"),
                     str(plan.sample_rate), str(plan.block_size)],
                    cwd=rec.directory,
                )
            except RuntimeError as exc:
                raise HTTPException(status_code=409, detail=str(exc)) from exc
            return {"mode": "realtime", "status": "started", "pid": session.proc.pid,
                    "built_components": built}

        if pace:
            # 离线实时播放：宿主按真实时长处理，探针每 200ms 流式上报（会话方式，UI 轮询）
            rt_exe = ensure_runtime_built()
            (rec.directory / "outputs").mkdir(exist_ok=True)
            session = rt_sessions.start(
                name,
                [str(rt_exe), str(plan_path), str(root / "build" / "components"),
                 "--pace", "--probe-interval", "200"],
                cwd=rec.directory,
            )
            return {"mode": "offline_live", "status": "started", "pid": session.proc.pid,
                    "built_components": built}

        exe = ensure_runtime_built()
        project_dir = rec.directory
        (project_dir / "outputs").mkdir(exist_ok=True)
        try:
            result = subprocess.run(
                [str(exe), str(plan_path), str(root / "build" / "components")],
                cwd=project_dir, capture_output=True, text=True,
                encoding="utf-8", errors="replace",
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
            "mode": "offline",
            "status": "ok" if result.returncode == 0 else "error",
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "built_components": built,
            "outputs": outputs,
            "probes": _parse_probe_lines(result.stdout),
        }

    @app.post("/api/projects/{name}/run_generated")
    def run_generated_project(name: str) -> dict[str, Any]:
        """Codegen run: generate a standalone C project, build it statically, run it."""
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        flat = flattened_project(rec)
        if any(n.component in DEVICE_COMPONENTS for n in flat.graph.nodes.values()):
            raise HTTPException(
                status_code=400,
                detail="生成模式暂不支持设备组件（生成宿主为文件时钟）",
            )
        plan, _ = compile_record(rec)

        gen_dir = rec.directory / "generated"
        CodeGenerator(registry, root).generate(plan, gen_dir)

        # reuse the toolchain of the main build (PATH may resolve a broken gcc)
        build_dir = gen_dir / "build"
        configure_args = ["cmake", "-S", str(gen_dir), "-B", str(build_dir), "-G", "Ninja"]
        cache = root / "build" / "CMakeCache.txt"
        if cache.exists():
            for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
                if line.startswith("CMAKE_C_COMPILER:") and "=" in line:
                    configure_args.append(f"-DCMAKE_C_COMPILER={line.split('=', 1)[1]}")

        configure = run_cmake_with_msvc_env(
            configure_args, cwd=rec.directory, build_dir=root / "build",
        )
        if configure.returncode != 0:
            raise HTTPException(status_code=500, detail=f"generated configure failed:\n{configure.stderr}")
        build = run_cmake_with_msvc_env(
            ["cmake", "--build", str(build_dir)], cwd=rec.directory, build_dir=root / "build",
        )
        if build.returncode != 0:
            raise HTTPException(
                status_code=500,
                detail=f"generated build failed:\n{build.stderr}\n{build.stdout}",
            )

        suffix = ".exe" if sys.platform == "win32" else ""
        exe = build_dir / f"orpheus_generated_app{suffix}"

        # match the offline host's duration: blocks = ceil(wav_in frames / block_size)
        blocks = 1000
        has_wav_in = False
        for node in flat.graph.nodes.values():
            if node.component == "orpheus.builtin.wav_in":
                has_wav_in = True
                fp = node.params.get("file_path")
                if fp:
                    frames = _wav_total_frames(rec.directory / str(fp))
                    if frames > 0:
                        blocks = (frames + plan.block_size - 1) // plan.block_size
                break
        if not has_wav_in and plan.duration_frames > 0:
            # 纯时钟图（扫频等）：按计划时长跑，避免 60s 扫频只跑默认 1000 块
            blocks = max(blocks, (plan.duration_frames + plan.block_size - 1) // plan.block_size)

        project_dir = rec.directory
        (project_dir / "outputs").mkdir(exist_ok=True)
        try:
            result = subprocess.run(
                [str(exe), str(blocks)], cwd=project_dir, capture_output=True, text=True,
                encoding="utf-8", errors="replace",
                timeout=RUN_TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired as exc:
            raise HTTPException(
                status_code=500, detail=f"generated app timed out after {RUN_TIMEOUT_SECONDS}s"
            ) from exc

        outputs = sorted(
            p.relative_to(project_dir).as_posix()
            for p in (project_dir / "outputs").rglob("*")
            if p.is_file()
        )
        return {
            "mode": "generated",
            "status": "ok" if result.returncode == 0 else "error",
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "blocks": blocks,
            "outputs": outputs,
        }

    @app.get("/api/projects/{name}/files")
    def list_project_files(name: str, ext: str | None = None) -> list[dict[str, Any]]:
        """List files inside a project dir (relative POSIX paths), optional extension filter."""
        try:
            pdir = manager.project_dir(name)
            if not pdir.exists():
                raise HTTPException(status_code=404, detail=f"project not found: {name}")
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        ext_filter = ext.lower() if ext else None
        items = []
        for p in sorted(pdir.rglob("*")):
            if not p.is_file():
                continue
            if ext_filter and p.suffix.lower() != ext_filter:
                continue
            items.append({"path": p.relative_to(pdir).as_posix(), "size": p.stat().st_size})
        return items

    @app.post("/api/projects/{name}/uploads", status_code=201)
    async def upload_project_file(name: str, file: UploadFile) -> dict[str, Any]:
        """Upload a file into the project dir; returns its project-relative path."""
        try:
            pdir = manager.project_dir(name)
            if not pdir.exists():
                raise HTTPException(status_code=404, detail=f"project not found: {name}")
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        filename = Path(file.filename or "upload.bin").name  # strip any client path
        if not filename:
            raise HTTPException(status_code=400, detail="empty filename")
        dest = pdir / filename
        with open(dest, "wb") as f:
            f.write(await file.read())
        return {"path": filename, "size": dest.stat().st_size}

    # ---- realtime sessions (rt_host subprocess)

    @app.post("/api/projects/{name}/rt/start")
    def rt_start(name: str) -> dict[str, Any]:
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        ensure_components_built(flattened_project(rec))
        plan, plan_path = compile_record(rec)
        suffix = ".exe" if sys.platform == "win32" else ""
        rt_exe = ensure_target_built("orpheus_rt_host", f"orpheus_rt_host{suffix}")
        try:
            session = rt_sessions.start(
                name,
                [str(rt_exe), str(plan_path), str(root / "build" / "components"),
                 str(plan.sample_rate), str(plan.block_size)],
                cwd=rec.directory,
            )
        except RuntimeError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc
        return {"status": "started", "pid": session.proc.pid}

    @app.post("/api/projects/{name}/rt/stop")
    def rt_stop(name: str) -> dict[str, Any]:
        session = rt_sessions.get(name)
        if session is None:
            raise HTTPException(status_code=404, detail="no realtime session for this project")
        session.stop()
        return {"status": "stopped", "exit_code": session.proc.poll()}

    @app.get("/api/projects/{name}/rt/status")
    def rt_status(name: str) -> dict[str, Any]:
        session = rt_sessions.get(name)
        if session is None:
            return {"running": False, "logs": [], "probes": {}}
        return session.snapshot()

    @app.post("/api/projects/{name}/rt/param")
    def rt_set_param(name: str, req: RtParamRequest) -> dict[str, Any]:
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        try:
            session.set_parameter(req.node, req.param, req.value)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"status": "sent"}

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
