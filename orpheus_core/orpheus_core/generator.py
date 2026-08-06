"""Code generator: produce a standalone C/C++ project from an Execution Plan."""

from __future__ import annotations

import json
import shutil
from pathlib import Path
from typing import Any

from orpheus_core.compiler import ExecutionPlan
from orpheus_core.project import Project
from orpheus_core.registry import Registry


class CodeGenerator:
    def __init__(self, registry: Registry, project_root: Path):
        self.registry = registry
        self.project_root = project_root

    def _component_target_name(self, component_id: str) -> str:
        return component_id.replace(".", "_")

    def _sanitized_node_id(self, node_id: str) -> str:
        return node_id.replace("-", "_").replace(" ", "_")

    @staticmethod
    def _c_escape(value: str) -> str:
        return value.replace("\\", "\\\\").replace('"', '\\"')

    def generate(self, plan: ExecutionPlan, output_dir: Path) -> None:
        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        components_dir = output_dir / "components"
        src_dir = output_dir / "src"
        include_dir = output_dir / "include"
        for d in [components_dir, src_dir, include_dir]:
            d.mkdir(parents=True, exist_ok=True)

        # The generated project must be self-contained: vendor the ABI header.
        abi_header = self.project_root / "orpheus_abi" / "include" / "orpheus_abi.h"
        shutil.copy2(abi_header, include_dir / "orpheus_abi.h")

        # Collect unique components
        component_ids = sorted({plan.node_configs[n]["component"] for n in plan.nodes})

        # Copy component sources
        for cid in component_ids:
            info = self.registry.get(cid)
            if info is None or info.package_type != "source":
                continue
            comp_target = self._component_target_name(cid)
            comp_out = components_dir / comp_target
            comp_out.mkdir(parents=True, exist_ok=True)
            if (info.root_dir / "src").exists():
                shutil.copytree(info.root_dir / "src", comp_out / "src", dirs_exist_ok=True)
            if (info.root_dir / "include").exists():
                shutil.copytree(info.root_dir / "include", comp_out / "include", dirs_exist_ok=True)
            # Third-party deps declared in the manifest are copied into the
            # component include dir so generated projects stay self-contained.
            if "miniaudio" in info.manifest.get("deps", []):
                ma_header = self.project_root / "third_party" / "miniaudio.h"
                if ma_header.exists():
                    shutil.copy2(ma_header, comp_out / "include" / "miniaudio.h")

        # Generate main.c
        self._generate_main_c(plan, component_ids, src_dir / "main.c")

        # Generate CMakeLists.txt
        self._generate_cmake(plan, component_ids, output_dir)

    def _generate_main_c(self, plan: ExecutionPlan, component_ids: list[str], path: Path) -> None:
        lines: list[str] = []
        lines.append('#include <stdio.h>')
        lines.append('#include <stdlib.h>')
        lines.append('#include <string.h>')
        lines.append('#include "orpheus_abi.h"')
        lines.append("")

        # Include component headers
        for cid in component_ids:
            comp_target = self._component_target_name(cid)
            header_name = f"orpheus_{comp_target.replace('orpheus_builtin_', '')}.h"
            # Simplified header name mapping
            short = cid.replace("orpheus.builtin.", "")
            header_name = f"orpheus_{short}.h"
            lines.append(f'#include "{header_name}"')
        lines.append("")

        # Per-component entry points: each component lib is compiled with
        # ORPHEUS_ENTRY_NAME=<target>_get_interface so static linking has no
        # symbol collision (dynamic loading keeps the default orpheus_get_interface).
        for cid in component_ids:
            comp_target = self._component_target_name(cid)
            lines.append(f'extern const OrpheusComponentInterface* {comp_target}_get_interface(void);')
        lines.append("")

        # State declarations
        for node_id in plan.execution_order:
            s = self._sanitized_node_id(node_id)
            lines.append(f'static void* g_state_{s} = NULL;')
            lines.append(f'static const OrpheusComponentInterface* g_iface_{s} = NULL;')
        lines.append("")

        # v2 统一内存拼接：manifest 声明 state_type（状态结构体公开）的组件，
        # 在生成工程中按类型静态拼接为 g_arena，零 malloc、布局由编译器决定。
        arena_members: list[tuple[str, str]] = []
        for node_id in plan.execution_order:
            cfg = plan.node_configs[node_id]
            info = self.registry.get(cfg["component"])
            st = info.manifest.get("state_type") if info else None
            if st:
                arena_members.append((self._sanitized_node_id(node_id), st))
        if arena_members:
            lines.append('static struct {')
            for s, st in arena_members:
                lines.append(f'    {st} {s};')
            lines.append('} g_arena;')
            lines.append('')

        # Buffer declarations
        for buf_id, buf in plan.buffers.items():
            s_buf = buf_id.replace("-", "_").replace(".", "_")
            samples = buf["frame_count"] * buf["channels"]
            lines.append(f'static float g_buf_{s_buf}[{samples}];')
            lines.append(f'static OrpheusBuffer g_buffer_{s_buf} = {{0}};')
        lines.append("")

        # Input/output buffer pointer arrays per node, sized by the ordered port
        # lists and bound by port id (unconnected pins stay NULL).
        port_buffer: dict[str, str] = {}  # "node:port" -> buffer global name
        fanout_buf: dict[str, str] = {}   # source "node:port" -> first buffer id
        for conn in plan.connections:
            s_buf = conn["buffer"].replace("-", "_").replace(".", "_")
            if conn["from"] not in fanout_buf:
                fanout_buf[conn["from"]] = s_buf
            s_buf = fanout_buf[conn["from"]]
            port_buffer[conn["from"]] = f'&g_buffer_{s_buf}'
            port_buffer[conn["to"]] = f'&g_buffer_{s_buf}'

        for node_id in plan.execution_order:
            s = self._sanitized_node_id(node_id)
            cfg = plan.node_configs[node_id]
            n_in = len(cfg.get("input_ports", []))
            n_out = len(cfg.get("output_ports", []))
            if n_in:
                lines.append(f'static OrpheusBuffer* g_inputs_{s}[{n_in}] = {{0}};')
            if n_out:
                lines.append(f'static OrpheusBuffer* g_outputs_{s}[{n_out}] = {{0}};')
        lines.append("")

        # Static parameter tables per node (ids + typed values)
        for node_id in plan.execution_order:
            s = self._sanitized_node_id(node_id)
            cfg = plan.node_configs[node_id]
            params = cfg.get("params", {})
            if not params:
                continue
            # 按 manifest 参数类型下发，避免字符串参数（如 gain_db "-6.0"）被当成 STRING
            ptypes: dict[str, str] = {}
            info = self.registry.get(cfg["component"])
            if info:
                for p in info.manifest.get("parameters", []):
                    if isinstance(p, dict) and p.get("id"):
                        ptypes[p["id"]] = p.get("type")
            ids = ", ".join(f'"{pid}"' for pid in params)
            lines.append(f'static const char* g_param_ids_{s}[] = {{{ids}}};')
            vals = []
            for pid, pval in params.items():
                ptype = ptypes.get(pid)
                if ptype == "float":
                    vals.append(f'{{ .type = ORPHEUS_VALUE_FLOAT, .value.f32 = {float(pval)}f }}')
                elif ptype == "int":
                    vals.append(f'{{ .type = ORPHEUS_VALUE_INT, .value.i32 = {int(pval)} }}')
                elif ptype == "bool":
                    vals.append(f'{{ .type = ORPHEUS_VALUE_BOOL, .value.b = {str(pval).lower()} }}')
                elif isinstance(pval, bool):
                    vals.append(f'{{ .type = ORPHEUS_VALUE_BOOL, .value.b = {str(pval).lower()} }}')
                elif isinstance(pval, int):
                    vals.append(f'{{ .type = ORPHEUS_VALUE_INT, .value.i32 = {pval} }}')
                elif isinstance(pval, float):
                    vals.append(f'{{ .type = ORPHEUS_VALUE_FLOAT, .value.f32 = {pval}f }}')
                else:
                    vals.append(
                        f'{{ .type = ORPHEUS_VALUE_STRING, .value.str = "{self._c_escape(str(pval))}" }}'
                    )
            lines.append(f'static OrpheusValue g_param_vals_{s}[] = {{')
            lines.append(f'    {", ".join(vals)}')
            lines.append('};')
        lines.append("")

        # Init function
        lines.append('static int orpheus_generated_init(uint32_t sample_rate, uint32_t block_size) {')
        lines.append('    int rc;')
        lines.append('    OrpheusConfig config;')
        lines.append('    config.sample_rate = sample_rate;')
        lines.append('    config.block_size = block_size;')
        lines.append('    config.param_ids = NULL;')
        lines.append('    config.param_values = NULL;')
        lines.append('    config.param_count = 0;')
        lines.append("")

        # Assign interface getters
        for node_id in plan.execution_order:
            cfg = plan.node_configs[node_id]
            s = self._sanitized_node_id(node_id)
            comp_target = self._component_target_name(cfg["component"])
            lines.append(f'    g_iface_{s} = {comp_target}_get_interface();')
        lines.append("")

        # Assign buffer pointer arrays by port id -> slot index
        for node_id in plan.execution_order:
            s = self._sanitized_node_id(node_id)
            cfg = plan.node_configs[node_id]
            for idx, port_id in enumerate(cfg.get("input_ports", [])):
                buf = port_buffer.get(f"{node_id}:{port_id}")
                if buf:
                    lines.append(f'    g_inputs_{s}[{idx}] = {buf};')
            for idx, port_id in enumerate(cfg.get("output_ports", [])):
                buf = port_buffer.get(f"{node_id}:{port_id}")
                if buf:
                    lines.append(f'    g_outputs_{s}[{idx}] = {buf};')
        lines.append("")

        # Initialize buffer structs
        for buf_id, buf in plan.buffers.items():
            s_buf = buf_id.replace("-", "_").replace(".", "_")
            lines.append(f'    g_buffer_{s_buf}.data = g_buf_{s_buf};')
            lines.append(f'    g_buffer_{s_buf}.format = ORPHEUS_FORMAT_F32;')
            lines.append(f'    g_buffer_{s_buf}.channels = {buf["channels"]};')
            lines.append(f'    g_buffer_{s_buf}.frame_capacity = {buf["frame_count"]};')
            lines.append(f'    g_buffer_{s_buf}.frame_count = {buf["frame_count"]};')
            lines.append(f'    g_buffer_{s_buf}.interleaved = 1;')
        lines.append("")

        # Create and prepare instances (with per-node parameter tables)
        for node_id in plan.execution_order:
            cfg = plan.node_configs[node_id]
            s = self._sanitized_node_id(node_id)
            params = cfg.get("params", {})
            channels = int(float(params.get("channels", 2)))
            lines.append(f'    config.channels = {channels};')
            if params:
                lines.append(f'    config.param_ids = g_param_ids_{s};')
                lines.append(f'    config.param_values = g_param_vals_{s};')
                lines.append(f'    config.param_count = {len(params)};')
            else:
                lines.append('    config.param_ids = NULL;')
                lines.append('    config.param_values = NULL;')
                lines.append('    config.param_count = 0;')
            info = self.registry.get(cfg["component"])
            st = info.manifest.get("state_type") if info else None
            if st:
                lines.append(f'    config.state_block = &g_arena.{s};')
            else:
                lines.append('    config.state_block = NULL;')
            lines.append(f'    rc = g_iface_{s}->create(&g_state_{s}, &config);')
            lines.append(f'    if (rc != ORPHEUS_OK) return rc;')
            lines.append(f'    rc = g_iface_{s}->prepare(g_state_{s}, &config);')
            lines.append(f'    if (rc != ORPHEUS_OK) return rc;')
        lines.append('    return ORPHEUS_OK;')
        lines.append('}')
        lines.append("")

        # Process function (multi-rate: per-node frames + divisor-gated firing)
        lines.append('static uint64_t g_block_counter = 0;')
        lines.append('static int orpheus_generated_process(uint32_t frame_count) {')
        lines.append('    int rc;')
        lines.append('    OrpheusProcessContext ctx;')
        lines.append(f'    ctx.sample_rate = {plan.sample_rate};')
        lines.append('    ctx.scratch = NULL;')
        lines.append('    ctx.scratch_size = 0;')
        lines.append('    ctx.timestamp = 0.0;')
        lines.append("")
        for node_id in plan.execution_order:
            cfg = plan.node_configs[node_id]
            s = self._sanitized_node_id(node_id)
            n_in = len(cfg.get("input_ports", []))
            n_out = len(cfg.get("output_ports", []))
            in_name = f'g_inputs_{s}' if n_in else 'NULL'
            out_name = f'g_outputs_{s}' if n_out else 'NULL'
            divisor = cfg.get("divisor", 1)
            frames = cfg.get("frames", 0)
            if divisor > 1:
                lines.append(f'    if ((g_block_counter + 1) % {divisor} == 0) {{')
            indent = '        ' if divisor > 1 else '    '
            lines.append(f'{indent}ctx.state = g_state_{s};')
            lines.append(f'{indent}ctx.frame_count = {frames} > 0 ? {frames} : frame_count;')
            lines.append(f'{indent}ctx.inputs = (const OrpheusBuffer* const*){in_name};')
            lines.append(f'{indent}ctx.outputs = {out_name};')
            lines.append(f'{indent}ctx.input_count = {n_in};')
            lines.append(f'{indent}ctx.output_count = {n_out};')
            lines.append(f'{indent}rc = g_iface_{s}->process(g_state_{s}, &ctx);')
            lines.append(f'{indent}if (rc != ORPHEUS_OK) return rc;')
            if divisor > 1:
                lines.append('    }')
        lines.append('    g_block_counter++;')
        lines.append('    return ORPHEUS_OK;')
        lines.append('}')
        lines.append("")

        # Main stub: argv[1] = number of blocks to process (default 1000)
        lines.append('int main(int argc, char** argv) {')
        lines.append('    int blocks = argc > 1 ? atoi(argv[1]) : 1000;')
        lines.append(f'    int rc = orpheus_generated_init({plan.sample_rate}, {plan.block_size});')
        lines.append('    if (rc != ORPHEUS_OK) {')
        lines.append('        fprintf(stderr, "init failed: %d\\n", rc);')
        lines.append('        return 1;')
        lines.append('    }')
        lines.append('    for (int i = 0; i < blocks; ++i) {')
        lines.append(f'        rc = orpheus_generated_process({plan.block_size});')
        lines.append('        if (rc != ORPHEUS_OK) return 1;')
        lines.append('    }')
        lines.append('    // Teardown: destroy instances so sinks flush output')
        lines.append('    // (e.g. wav_out writes the file in destroy).')
        for node_id in reversed(plan.execution_order):
            s = self._sanitized_node_id(node_id)
            lines.append(f'    g_iface_{s}->destroy(g_state_{s});')
        lines.append('    return 0;')
        lines.append('}')

        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))

    def _generate_cmake(self, plan: ExecutionPlan, component_ids: list[str], output_dir: Path) -> None:
        lines: list[str] = []
        lines.append('cmake_minimum_required(VERSION 3.16)')
        lines.append('project(orpheus_generated C)')
        lines.append('set(CMAKE_C_STANDARD 11)')
        lines.append('set(CMAKE_C_STANDARD_REQUIRED ON)')
        lines.append("")
        lines.append('include_directories(${CMAKE_SOURCE_DIR}/include)')
        lines.append('add_definitions(-DORPHEUS_API=)')
        lines.append("")
        # MSVC 必须按 UTF-8 读取源码（中文注释/字符串），与主构建保持一致
        lines.append('if(MSVC)')
        lines.append('  add_compile_options(/utf-8)')
        lines.append('endif()')
        lines.append("")

        for cid in component_ids:
            info = self.registry.get(cid)
            if info is None or info.package_type != "source":
                continue
            comp_target = self._component_target_name(cid)
            short = cid.replace("orpheus.builtin.", "")
            lines.append(f'add_library({comp_target} STATIC)')
            sources = info.manifest.get("sources") or [f"src/{short}.c"]
            for src in sources:
                lines.append(f'target_sources({comp_target} PRIVATE components/{comp_target}/{src})')
            lines.append(f'target_include_directories({comp_target} PUBLIC components/{comp_target}/include)')
            lines.append(
                f'target_compile_definitions({comp_target} PRIVATE ORPHEUS_ENTRY_NAME={comp_target}_get_interface)'
            )
            lines.append("")

        lines.append('add_executable(orpheus_generated_app src/main.c)')
        libs = " ".join(self._component_target_name(cid) for cid in component_ids)
        lines.append(f'target_link_libraries(orpheus_generated_app {libs})')

        with open(output_dir / "CMakeLists.txt", "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
