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

    def generate(self, plan: ExecutionPlan, output_dir: Path) -> None:
        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        components_dir = output_dir / "components"
        src_dir = output_dir / "src"
        include_dir = output_dir / "include"
        for d in [components_dir, src_dir, include_dir]:
            d.mkdir(parents=True, exist_ok=True)

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

        # External component interface getters
        lines.append('extern const OrpheusComponentInterface* orpheus_get_interface(void);')
        for cid in component_ids:
            comp_target = self._component_target_name(cid)
            lines.append(f'static const OrpheusComponentInterface* {comp_target}_get_interface(void) {{ return orpheus_get_interface(); }}')
        lines.append("")

        # State declarations
        for node_id in plan.execution_order:
            s = self._sanitized_node_id(node_id)
            lines.append(f'static void* g_state_{s} = NULL;')
            lines.append(f'static const OrpheusComponentInterface* g_iface_{s} = NULL;')
        lines.append("")

        # Buffer declarations
        for buf_id, buf in plan.buffers.items():
            s_buf = buf_id.replace("-", "_").replace(".", "_")
            samples = buf["frame_count"] * buf["channels"]
            lines.append(f'static float g_buf_{s_buf}[{samples}];')
            lines.append(f'static OrpheusBuffer g_buffer_{s_buf} = {{0}};')
        lines.append("")

        # Input/output buffer pointer arrays per node
        for node_id in plan.execution_order:
            s = self._sanitized_node_id(node_id)
            in_bufs = []
            out_bufs = []
            for conn in plan.connections:
                from_node, from_port = conn["from"].split(":")
                to_node, to_port = conn["to"].split(":")
                s_buf = conn["buffer"].replace("-", "_").replace(".", "_")
                if from_node == node_id:
                    out_bufs.append(f'&g_buffer_{s_buf}')
                if to_node == node_id:
                    in_bufs.append(f'&g_buffer_{s_buf}')
            if in_bufs:
                lines.append(f'static OrpheusBuffer* g_inputs_{s}[{len(in_bufs)}];')
            if out_bufs:
                lines.append(f'static OrpheusBuffer* g_outputs_{s}[{len(out_bufs)}];')
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

        # Assign buffer pointer arrays
        for node_id in plan.execution_order:
            s = self._sanitized_node_id(node_id)
            in_idx = 0
            out_idx = 0
            for conn in plan.connections:
                from_node, from_port = conn["from"].split(":")
                to_node, to_port = conn["to"].split(":")
                s_buf = conn["buffer"].replace("-", "_").replace(".", "_")
                if from_node == node_id:
                    lines.append(f'    g_outputs_{s}[{out_idx}] = &g_buffer_{s_buf};')
                    out_idx += 1
                if to_node == node_id:
                    lines.append(f'    g_inputs_{s}[{in_idx}] = &g_buffer_{s_buf};')
                    in_idx += 1
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

        # Create and prepare instances
        for node_id in plan.execution_order:
            cfg = plan.node_configs[node_id]
            s = self._sanitized_node_id(node_id)
            comp = cfg["component"]
            info = self.registry.get(comp)
            channels = 2
            for pid, pval in cfg.get("params", {}).items():
                if pid == "channels":
                    channels = int(float(pval))
            lines.append(f'    config.channels = {channels};')
            lines.append(f'    rc = g_iface_{s}->create(&g_state_{s}, &config);')
            lines.append(f'    if (rc != ORPHEUS_OK) return rc;')
            lines.append(f'    rc = g_iface_{s}->prepare(g_state_{s}, &config);')
            lines.append(f'    if (rc != ORPHEUS_OK) return rc;')
        lines.append('    return ORPHEUS_OK;')
        lines.append('}')
        lines.append("")

        # Process function
        lines.append('static int orpheus_generated_process(uint32_t frame_count) {')
        lines.append('    int rc;')
        lines.append('    OrpheusProcessContext ctx;')
        lines.append('    ctx.frame_count = frame_count;')
        lines.append(f'    ctx.sample_rate = {plan.sample_rate};')
        lines.append('    ctx.scratch = NULL;')
        lines.append('    ctx.scratch_size = 0;')
        lines.append('    ctx.timestamp = 0.0;')
        lines.append("")
        for node_id in plan.execution_order:
            cfg = plan.node_configs[node_id]
            s = self._sanitized_node_id(node_id)
            in_bufs = []
            out_bufs = []
            for conn in plan.connections:
                from_node, from_port = conn["from"].split(":")
                to_node, to_port = conn["to"].split(":")
                if from_node == node_id:
                    out_bufs.append(f'g_outputs_{s}')
                if to_node == node_id:
                    in_bufs.append(f'g_inputs_{s}')
            # Dedup
            in_name = in_bufs[0] if in_bufs else 'NULL'
            out_name = out_bufs[0] if out_bufs else 'NULL'
            in_count = len(set(in_bufs)) if in_bufs else 0
            out_count = len(set(out_bufs)) if out_bufs else 0
            lines.append(f'    ctx.state = g_state_{s};')
            lines.append(f'    ctx.inputs = (const OrpheusBuffer* const*){in_name};')
            lines.append(f'    ctx.outputs = {out_name};')
            lines.append(f'    ctx.input_count = {in_count};')
            lines.append(f'    ctx.output_count = {out_count};')
            lines.append(f'    rc = g_iface_{s}->process(g_state_{s}, &ctx);')
            lines.append(f'    if (rc != ORPHEUS_OK) return rc;')
        lines.append('    return ORPHEUS_OK;')
        lines.append('}')
        lines.append("")

        # Main stub
        lines.append('int main(int argc, char** argv) {')
        lines.append('    (void)argc; (void)argv;')
        lines.append(f'    int rc = orpheus_generated_init({plan.sample_rate}, {plan.block_size});')
        lines.append('    if (rc != ORPHEUS_OK) {')
        lines.append('        fprintf(stderr, "init failed: %d\\n", rc);')
        lines.append('        return 1;')
        lines.append('    }')
        lines.append(f'    for (int i = 0; i < 1000; ++i) {{')
        lines.append(f'        rc = orpheus_generated_process({plan.block_size});')
        lines.append('        if (rc != ORPHEUS_OK) return 1;')
        lines.append('    }')
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
        lines.append('include_directories(${CMAKE_SOURCE_DIR}/../orpheus_abi/include)')
        lines.append('add_definitions(-DORPHEUS_API=)')
        lines.append("")

        for cid in component_ids:
            comp_target = self._component_target_name(cid)
            short = cid.replace("orpheus.builtin.", "")
            lines.append(f'add_library({comp_target} STATIC)')
            lines.append(f'target_sources({comp_target} PRIVATE components/{comp_target}/src/{short}.c)')
            lines.append(f'target_include_directories({comp_target} PUBLIC components/{comp_target}/include)')
            lines.append("")

        lines.append('add_executable(orpheus_generated_app src/main.c)')
        libs = " ".join(self._component_target_name(cid) for cid in component_ids)
        lines.append(f'target_link_libraries(orpheus_generated_app {libs})')

        with open(output_dir / "CMakeLists.txt", "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
