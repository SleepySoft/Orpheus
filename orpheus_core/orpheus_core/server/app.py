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
import yaml

from fastapi import Body, FastAPI, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel

from orpheus_core.builder import BuildError, ComponentBuilder, run_cmake_with_msvc_env
from orpheus_core.compiler import CompileError, ExecutionPlan, GraphCompiler
from orpheus_core.distill_topology import build_topology
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


class RtBulkRequest(BaseModel):
    node: str
    key: str
    values: list[float]


class RtIdRequest(BaseModel):
    id: int


class RtWriteRequest(BaseModel):
    id: int
    value: float | int | str


class RtWriteBulkRequest(BaseModel):
    id: int
    values: list[float]


class RtReadBulkRequest(BaseModel):
    node: str | None = None
    key: str | None = None
    id: int | None = None


class RtMsgRequest(BaseModel):
    hex: str


class DistillImportRequest(BaseModel):
    yaml: str


class ProjectNotesRequest(BaseModel):
    content: str


class NodeNotesRequest(BaseModel):
    notes: dict[str, str]


def _component_to_dict(info: ComponentInfo) -> dict[str, Any]:
    m = info.manifest
    return {
        "id": info.id,
        "version": info.version,
        "abi_version": info.abi_version,
        "package_type": info.package_type,
        "name": m.get("name", info.id),
        "category": m.get("category", "未分类"),
        "order": m.get("order", 0),
        "description": m.get("description", ""),
        "clock_source": m.get("clock_source", False),
        "ports": m.get("ports", []),
        "parameters": m.get("parameters", []),
        "bulk_slots": m.get("bulk_slots", []),
        "custom_handles": m.get("custom_handles", []),
        "user_owned": m.get("user_owned", False),
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

    def generate_record(rec: ProjectRecord) -> tuple[ExecutionPlan, Path]:
        """生成独立 C 工程（不构建/运行）。嵌入 I/O 占位组件（embed_in/embed_out）允许。"""
        flat = flattened_project(rec)
        if any(n.component in DEVICE_COMPONENTS for n in flat.graph.nodes.values()):
            raise HTTPException(
                status_code=400,
                detail="生成模式暂不支持设备组件（生成宿主为文件时钟）",
            )
        plan, _ = compile_record(rec)
        gen_dir = rec.directory / "generated"
        CodeGenerator(registry, root).generate(plan, gen_dir)
        return plan, gen_dir

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

    @app.get("/api/components/{component_id}/readme")
    def get_component_readme(component_id: str) -> FileResponse:
        """返回组件目录下的 README.md；不存在则 404。"""
        info = registry.get(component_id)
        if info is None:
            raise HTTPException(status_code=404, detail=f"组件不存在: {component_id}")
        readme_path = info.root_dir / "README.md"
        if not readme_path.is_file():
            raise HTTPException(status_code=404, detail=f"组件暂无 README: {component_id}")
        return FileResponse(readme_path, media_type="text/markdown; charset=utf-8")

    @app.post("/api/components/rescan")
    def rescan_components() -> dict[str, Any]:
        registry.scan()
        return {"count": len(registry.list_components())}

    @app.delete("/api/components/{component_id}")
    def delete_component(component_id: str) -> dict[str, Any]:
        """删除用户自定义组件（需 user_owned；公共库组件拒绝）。"""
        info = registry.get(component_id)
        if info is None:
            raise HTTPException(status_code=404, detail=f"组件不存在: {component_id}")
        if not info.manifest.get("user_owned", False):
            raise HTTPException(status_code=400, detail="公共库组件不可删除")
        # 防护：仍被工程（含子组件图）引用的组件禁止删除
        refs: list[str] = []
        for pdir in sorted((root / "workspace").glob("*/project.yaml")):
            try:
                data = yaml.safe_load(pdir.read_text(encoding="utf-8"))
            except Exception:
                continue
            graphs = [(data or {}).get("graph") or {}]
            for sub in ((data or {}).get("subcomponents") or []):
                graphs.append(sub.get("graph") or {})
            if any(
                n.get("component") == component_id
                for g in graphs
                for n in (g.get("nodes") or [])
            ):
                refs.append(pdir.parent.name)
        if refs:
            raise HTTPException(
                status_code=400,
                detail=f"组件仍被工程 {', '.join(sorted(refs))} 引用，请先移除这些工程中的节点再删除",
            )
        shutil.rmtree(info.root_dir)
        registry.scan()
        return {"deleted": component_id}

    @app.post("/api/components/{component_id}/promote")
    def promote_component(component_id: str) -> dict[str, Any]:
        """把用户自定义组件提升为公共库（user_owned=false，之后不可直接删除）。"""
        info = registry.get(component_id)
        if info is None:
            raise HTTPException(status_code=404, detail=f"组件不存在: {component_id}")
        yaml_path = info.root_dir / "component.yaml"
        data = yaml.safe_load(yaml_path.read_text(encoding="utf-8"))
        data["user_owned"] = False
        with open(yaml_path, "w", encoding="utf-8") as f:
            yaml.safe_dump(data, f, sort_keys=False, allow_unicode=True)
        registry.scan()
        return {"promoted": component_id}

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

    @app.post("/api/projects/{name}/distill")
    def import_distilled_model(name: str, req: DistillImportRequest) -> dict[str, Any]:
        """一键导入蒸馏模型：接受 Orpheus 工程 YAML（含嵌套子组件与 model_tree 注释），
        校验后创建为新工程；顶层未知字段（presets/model_tree 等）原样保留。"""
        try:
            data = yaml.safe_load(req.yaml)
        except yaml.YAMLError as exc:
            raise HTTPException(status_code=400, detail=f"YAML 解析失败: {exc}") from exc
        if not isinstance(data, dict) or "graph" not in data:
            raise HTTPException(
                status_code=400,
                detail="蒸馏模型文件缺少 graph（应为 Orpheus 工程 YAML：version + graph[+ subcomponents]）",
            )
        data.setdefault("version", "0.1.0")
        data.setdefault("metadata", {})
        data["metadata"]["name"] = name
        # 蒸馏拓扑展开：model_tree.chains 存在且原图为骨架时，把每条链展开为
        # 子模块（块 → 节点，未映射块用占位组件 id，UI 标红「组件缺失」）。
        mt = data.get("model_tree")
        existing_graph = data.get("graph") or {}
        if isinstance(mt, dict) and mt.get("chains") and len(existing_graph.get("nodes") or []) <= 3:
            channels = 22
            for n in existing_graph.get("nodes") or []:
                if n.get("component") == "orpheus.builtin.embed_in":
                    channels = (n.get("params") or {}).get("channels", channels)
                    break
            graph, subs = build_topology(
                mt,
                sample_rate=data.get("sample_rate", 48000),
                channels=channels,
            )
            data["graph"] = graph
            data["subcomponents"] = subs
        try:
            manager.create(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        try:
            manager.put(name, data)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        return {"name": name, "document": manager.get_document(name)}

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

    @app.get("/api/projects/{name}/notes")
    def get_project_notes(name: str) -> dict[str, Any]:
        """读取工程侧车笔记 notes.md（与 project.yaml 并置）。"""
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        notes_path = rec.path.with_name("notes.md")
        if not notes_path.is_file():
            raise HTTPException(status_code=404, detail="工程笔记不存在")
        return {"content": notes_path.read_text(encoding="utf-8")}

    @app.put("/api/projects/{name}/notes")
    def put_project_notes(name: str, req: ProjectNotesRequest) -> dict[str, Any]:
        """保存工程侧车笔记 notes.md。"""
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        notes_path = rec.path.with_name("notes.md")
        notes_path.write_text(req.content, encoding="utf-8")
        return {"status": "saved", "path": str(notes_path)}

    @app.get("/api/projects/{name}/node_notes")
    def get_node_notes(name: str) -> dict[str, Any]:
        """读取工程节点笔记 sidecar（node-notes.json）。"""
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        notes_path = rec.path.with_name("node-notes.json")
        if not notes_path.is_file():
            return {"notes": {}}
        try:
            notes = json.loads(notes_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise HTTPException(status_code=500, detail=f"node-notes.json 解析失败: {exc}") from exc
        return {"notes": notes}

    @app.put("/api/projects/{name}/node_notes")
    def put_node_notes(name: str, req: NodeNotesRequest) -> dict[str, Any]:
        """保存工程节点笔记 sidecar（node-notes.json）。"""
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        notes_path = rec.path.with_name("node-notes.json")
        notes_path.write_text(
            json.dumps(req.notes, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        return {"status": "saved", "path": str(notes_path)}

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
            # 数据点 ID 表（用途/形式/类型/个数），供 UI 显示 0x ID 与按 ID 解析/控制
            "id_map": plan.id_map,
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
        plan, gen_dir = generate_record(rec)

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
        for cfg in plan.node_configs.values():
            if cfg["component"] == "orpheus.builtin.wav_in":
                has_wav_in = True
                fp = cfg["params"].get("file_path")
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
            "generated_path": str(gen_dir.relative_to(rec.directory)),
            "download_url": f"/api/projects/{name}/generated/archive",
        }

    @app.post("/api/projects/{name}/generate")
    def generate_project(name: str) -> dict[str, Any]:
        """只生成独立 C 工程（不构建/运行）：嵌入部署导出代码（含 platform_io.c 适配模板）。"""
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        plan, gen_dir = generate_record(rec)
        return {
            "status": "ok",
            "generated_dir": str(gen_dir),
            "generated_path": str(gen_dir.relative_to(rec.directory)),
            "download_url": f"/api/projects/{name}/generated/archive",
            "blocks_default": plan.duration_frames > 0
            and (plan.duration_frames + plan.block_size - 1) // plan.block_size
            or 1000,
        }

    @app.get("/api/projects/{name}/generated/archive")
    def download_generated_code(name: str) -> FileResponse:
        """下载生成工程目录（workspace/<name>/generated）为 zip，供嵌入部署取用。"""
        try:
            rec = manager.get(name)
        except ProjectError as exc:
            raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
        gen_dir = rec.directory / "generated"
        if not gen_dir.exists():
            raise HTTPException(
                status_code=404,
                detail="尚未生成代码：先点「⤓ 生成代码」或「⚙ 编译后运行」",
            )
        archive = shutil.make_archive(
            str(Path(tempfile.gettempdir()) / f"orpheus_{name}_generated"),
            "zip",
            root_dir=gen_dir,
        )
        return FileResponse(archive, filename=f"{name}_generated.zip")

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

    @app.post("/api/projects/{name}/rt/bulk")
    def rt_write_bulk(name: str, req: RtBulkRequest) -> dict[str, Any]:
        """实时会话 BULK 直写：把数值数组写入组件注册的 BULK 槽（如 biquad_bank 系数）。"""
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        try:
            session.write_bulk(req.node, req.key, req.values)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"status": "sent"}

    @app.get("/api/projects/{name}/rt/resolve")
    def rt_resolve_id(name: str, id: int) -> dict[str, Any]:
        """内存透明：按 32 位数据 ID 查询类型/长度/基址/偏移。"""
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        try:
            return session.resolve(id)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc

    @app.get("/api/projects/{name}/rt/map")
    def rt_map(name: str) -> dict[str, Any]:
        """dump 全表：所有数据点 + 模块包（含真实基址）。"""
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        try:
            return {"entries": session.map_all()}
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc

    @app.post("/api/projects/{name}/rt/write")
    def rt_write_id(name: str, req: RtWriteRequest) -> dict[str, Any]:
        """按 ID 实时控制（RTC 通道）：方向只在接口，PROBE/STATE 由注册表拒写。"""
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        try:
            session.write_id(req.id, req.value)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"status": "sent", "id": req.id}

    @app.post("/api/projects/{name}/rt/read")
    def rt_read_id(name: str, req: RtIdRequest) -> dict[str, Any]:
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        try:
            value = session.read_id(req.id)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"id": req.id, "value": value}

    @app.post("/api/projects/{name}/rt/write_bulk")
    def rt_write_bulk_id(name: str, req: RtWriteBulkRequest) -> dict[str, Any]:
        """按 ID 直写 BULK 槽（如 biquad_bank 系数）。"""
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        try:
            session.write_bulk_id(req.id, req.values)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"status": "sent", "id": req.id}

    @app.post("/api/projects/{name}/rt/read_bulk")
    def rt_read_bulk(name: str, req: RtReadBulkRequest) -> dict[str, Any]:
        """BULK 读回（active bank）：按 ID 或 (node, key)；越界检查后仅拷贝。"""
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        if req.id is None and (req.node is None or req.key is None):
            raise HTTPException(status_code=400, detail="需提供 id 或 (node, key)")
        try:
            values = session.read_bulk(node=req.node, key=req.key, data_id=req.id)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"id": req.id, "values": values}

    @app.post("/api/projects/{name}/rt/msg")
    def rt_msg(name: str, req: RtMsgRequest) -> dict[str, Any]:
        """二进制消息：CALL → 同步 RESPONSE（按 call_id 匹配），NOTIFICATION → 无返回。"""
        session = rt_sessions.get(name)
        if session is None or not session.running:
            raise HTTPException(status_code=400, detail="realtime session not running")
        try:
            frame = bytes.fromhex(req.hex)
        except ValueError as exc:
            raise HTTPException(status_code=400, detail="无效 hex") from exc
        if len(frame) < 8:
            raise HTTPException(status_code=400, detail="消息过短（至少 8 字节头）")
        bits = int.from_bytes(frame[4:8], "little")
        call_id = (bits >> 10) & 0xFFFF
        try:
            rsp = session.msg(req.hex, call_id)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"call_id": call_id, "response_hex": rsp or None}

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
