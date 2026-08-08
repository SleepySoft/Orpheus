"""Code generator: produce a standalone C/C++ project from an Execution Plan."""

from __future__ import annotations

import json
import re
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
        # 节点 id 可能来自用户/子组件展开（如 "fx__g"、"my.node"），
        # 必须清洗成合法 C 标识符，否则生成代码编译失败。
        return re.sub(r"[^A-Za-z0-9_]", "_", node_id)

    @staticmethod
    def _c_escape(value: str) -> str:
        return value.replace("\\", "\\\\").replace('"', '\\"')

    # ------------------------------------------------------------ ID / 模块布局辅助

    def _state_type(self, node_id: str, plan: ExecutionPlan) -> str | None:
        info = self.registry.get(plan.node_configs[node_id]["component"])
        return info.manifest.get("state_type") if info else None

    def _arena_member_chain(self, node_id: str) -> str:
        """叶子在嵌套 arena 中的成员链（模块段.叶子段），如 front.eq_bank.bq。"""
        return ".".join(self._sanitized_node_id(seg) for seg in node_id.split("__"))

    def _state_ref(self, node_id: str, plan: ExecutionPlan) -> str | None:
        """叶子状态表达式：&g_arena.<成员链>；无公开状态结构体时返回 None。"""
        if self._state_type(node_id, plan) is None:
            return None
        return "&g_arena." + self._arena_member_chain(node_id)

    def _camel(self, s: str) -> str:
        """任意分隔名 → CamelCase 标识符（模块/参数宏名用）。"""
        return "".join(p[:1].upper() + p[1:] for p in re.split(r"[^A-Za-z0-9]+", s) if p)

    def _module_children(self, modules: dict[str, dict], path: str) -> list[str]:
        prefix = path + "__" if path else ""
        return sorted(
            q for q in modules
            if q != path and q.startswith(prefix) and "__" not in q[len(prefix):]
        )

    def _module_type_name(self, path: str) -> str:
        return "OrpheusArena" if not path else "OrpheusMod_" + self._camel(path)

    def _module_member(self, path: str) -> str:
        return self._sanitized_node_id(path.split("__")[-1])

    _KIND_BITS = {"TUNE": 0x0, "CMD": 0x1, "PROBE": 0x2, "BULK": 0x3,
                  "STATE": 0x4, "MODULE": 0x5, "CUSTOM": 0x6}

    def _point_kind(self, p: dict) -> str:
        k = p.get("kind")
        if k:
            return {"setting": "TUNE", "command": "CMD", "probe": "PROBE",
                    "bulk": "BULK", "state": "STATE"}.get(k, "TUNE")
        if p.get("readback") and not p.get("persistent") and not p.get("affects_signature"):
            return "PROBE"
        return "TUNE"

    def _ctype_of(self, ptype: str) -> str:
        return {"float": "float", "int": "int32_t", "bool": "bool",
                "string": "const char*"}.get(ptype, "float")

    def _value_type_of(self, ptype: str) -> str:
        return {"float": "ORPHEUS_VALUE_FLOAT", "int": "ORPHEUS_VALUE_INT",
                "bool": "ORPHEUS_VALUE_BOOL",
                "string": "ORPHEUS_VALUE_STRING"}.get(ptype, "ORPHEUS_VALUE_FLOAT")

    def _bytes_of(self, ctype: str) -> int:
        return {"float": 4, "int32_t": 4, "bool": 1, "const char*": 8}.get(ctype, 4)

    def _id_value(self, kind: str, module_id: int, slot: int) -> int:
        return (self._KIND_BITS[kind] << 28) | ((module_id & 0xFF) << 16) | (slot & 0xFFFF)

    def _module_data_points(self, plan: ExecutionPlan, module: dict) -> list[dict]:
        """模块内数据点：叶子按执行序 × manifest 参数序 + bulk_slots 序（槽序号同此顺序）。"""
        points: list[dict] = []
        for leaf in module.get("leaves", []):
            nid = leaf["node"]
            cfg = plan.node_configs[nid]
            info = self.registry.get(cfg["component"])
            for p in (info.manifest.get("parameters", []) if info else []):
                points.append({**p, "node": nid})
            for bs in (info.manifest.get("bulk_slots", []) if info else []):
                points.append({**bs, "node": nid, "runtime": True})
        return points

    def _point_macro_name(self, module: dict, p: dict) -> str:
        """数据点宏名：模块Camel + (叶子Camel，仅模块多叶子时) + 参数Camel。
        单叶子模块（如 front__eq_bank）得到公司风格的 模块+参数 命名。"""
        mod_camel = self._camel(module["path"])
        param_camel = self._camel(p["id"])
        include_node = len(module.get("leaves", [])) > 1
        if include_node:
            node_camel = self._camel(p["node"].split("__")[-1])
            return f"{mod_camel}{node_camel}{param_camel}"
        return f"{mod_camel}{param_camel}"

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

        # 组件级 deps 闭包：biquad_bank 依赖 biquad 等——依赖组件的
        # 源码/头文件随生成工程递归复制，include 路径对依赖方可见。
        all_ids: list[str] = list(component_ids)
        seen: set[str] = set(component_ids)
        queue = list(component_ids)
        while queue:
            cid = queue.pop()
            info = self.registry.get(cid)
            if info is None:
                continue
            for d in info.manifest.get("deps", []):
                if self.registry.get(d) is not None and d not in seen:
                    seen.add(d)
                    all_ids.append(d)
                    queue.append(d)
        all_ids = sorted(all_ids)

        # Copy component sources (plan 组件 + 依赖闭包)
        for cid in all_ids:
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

        # 嵌入 I/O 适配模板：存在 embed_in/embed_out 节点时生成，用户按硬件填充
        embed_nodes = [
            nid for nid in plan.execution_order
            if plan.node_configs[nid]["component"]
            in ("orpheus.builtin.embed_in", "orpheus.builtin.embed_out")
        ]
        if embed_nodes:
            self._generate_platform_io(plan, src_dir / "platform_io.c")

        # 数据 ID（32 位宏）+ ID map + 内存布局（模块嵌套 arena 定义在 include/orpheus_arena.h）
        self._generate_ids(plan, include_dir, src_dir, output_dir)

        # Generate CMakeLists.txt
        self._generate_cmake(plan, all_ids, output_dir)

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
        if any(self._state_type(nid, plan) for nid in plan.execution_order):
            lines.append('#include "orpheus_arena.h"')
        lines.append("")

        # Per-component entry points: each component lib is compiled with
        # ORPHEUS_ENTRY_NAME=<target>_get_interface so static linking has no
        # symbol collision (dynamic loading keeps the default orpheus_get_interface).
        for cid in component_ids:
            comp_target = self._component_target_name(cid)
            lines.append(f'extern const OrpheusComponentInterface* {comp_target}_get_interface(void);')
        embed_in_nodes = [
            nid for nid in plan.execution_order
            if plan.node_configs[nid]["component"] == "orpheus.builtin.embed_in"
        ]
        embed_out_nodes = [
            nid for nid in plan.execution_order
            if plan.node_configs[nid]["component"] == "orpheus.builtin.embed_out"
        ]
        if embed_in_nodes or embed_out_nodes:
            lines.append('')
            lines.append('/* 嵌入 I/O 适配（platform_io.c，用户按实际硬件填充） */')
            lines.append('void orpheus_platform_io_init(void);')
            lines.append('void orpheus_platform_io_pre_block(void);')
            lines.append('void orpheus_platform_io_post_block(void);')
        lines.append("")

        # State declarations
        for node_id in plan.execution_order:
            s = self._sanitized_node_id(node_id)
            lines.append(f'static void* g_state_{s} = NULL;')
            lines.append(f'static const OrpheusComponentInterface* g_iface_{s} = NULL;')
        lines.append("")

        # v2 统一内存拼接：按模块嵌套结构体（每个子模块实例一块连续内存），
        # 类型定义在 include/orpheus_arena.h，布局由 C 编译器决定。
        if any(self._state_type(nid, plan) for nid in plan.execution_order):
            lines.append('static OrpheusArena g_arena;')
            lines.append('')

        # 嵌入 I/O 缓冲（用户 DMA 可直达）与状态访问器（platform_io.c 引用）
        for nid in embed_in_nodes + embed_out_nodes:
            s = self._sanitized_node_id(nid)
            cfg = plan.node_configs[nid]
            frames = cfg.get("frames") or plan.block_size
            channels = int(float(cfg.get("params", {}).get("channels", 2)))
            kind = "in" if nid in embed_in_nodes else "out"
            lines.append(f'float g_embed_{kind}_{s}[{frames * channels}];')
        if embed_in_nodes or embed_out_nodes:
            lines.append('')
        for nid in embed_in_nodes:
            s = self._sanitized_node_id(nid)
            lines.append(
                f'EmbedInState* orpheus_embed_in_state_{s}(void) '
                f'{{ return {self._state_ref(nid, plan)}; }}'
            )
        for nid in embed_out_nodes:
            s = self._sanitized_node_id(nid)
            lines.append(
                f'EmbedOutState* orpheus_embed_out_state_{s}(void) '
                f'{{ return {self._state_ref(nid, plan)}; }}'
            )
        if embed_in_nodes or embed_out_nodes:
            lines.append('')

        # 生成路径注册器：与动态路径一样调用 register_slots（当前无消费方，
        # 仅保证注册流程对等；bulk/控制通路启用后在此扩展）。
        lines.append('static uint64_t g_slot_seq = 0;')
        lines.append('static OrpheusSlotId generated_reg_add(void* ctx, const OrpheusSlotInfo* info) {')
        lines.append('    (void)ctx; (void)info;')
        lines.append('    return g_slot_seq++;')
        lines.append('}')
        lines.append('static OrpheusRegistry g_reg = { NULL, generated_reg_add, NULL };')
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
            st = self._state_type(node_id, plan)
            if st:
                lines.append(f'    config.state_block = {self._state_ref(node_id, plan)};')
            else:
                lines.append('    config.state_block = NULL;')
            lines.append(f'    rc = g_iface_{s}->create(&g_state_{s}, &config);')
            lines.append(f'    if (rc != ORPHEUS_OK) return rc;')
            lines.append(f'    if (g_iface_{s}->get_descriptor()->abi_version >= 2 && g_iface_{s}->register_slots) {{')
            lines.append(f'        g_iface_{s}->register_slots(g_state_{s}, &g_reg);')
            lines.append('    }')
            lines.append(f'    rc = g_iface_{s}->prepare(g_state_{s}, &config);')
            lines.append(f'    if (rc != ORPHEUS_OK) return rc;')
        for nid in embed_in_nodes:
            s = self._sanitized_node_id(nid)
            cfg = plan.node_configs[nid]
            frames = cfg.get("frames") or plan.block_size
            ref = self._state_ref(nid, plan)
            lines.append(f'    ({ref})->src = g_embed_in_{s};')
            lines.append(f'    ({ref})->src_frames = 0;')
        for nid in embed_out_nodes:
            s = self._sanitized_node_id(nid)
            cfg = plan.node_configs[nid]
            frames = cfg.get("frames") or plan.block_size
            ref = self._state_ref(nid, plan)
            lines.append(f'    ({ref})->dst = g_embed_out_{s};')
            lines.append(f'    ({ref})->dst_capacity = {frames};')
        if embed_in_nodes or embed_out_nodes:
            lines.append('    orpheus_platform_io_init();')
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
        if embed_in_nodes or embed_out_nodes:
            lines.append('    orpheus_platform_io_pre_block();')
            lines.append('')
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
        if embed_in_nodes or embed_out_nodes:
            lines.append('')
            lines.append('    orpheus_platform_io_post_block();')
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

    def _generate_platform_io(self, plan: ExecutionPlan, path: Path) -> None:
        """生成嵌入 I/O 适配模板：三个 USER CODE 函数，用户按实际硬件填充。"""
        in_nodes = [
            nid for nid in plan.execution_order
            if plan.node_configs[nid]["component"] == "orpheus.builtin.embed_in"
        ]
        out_nodes = [
            nid for nid in plan.execution_order
            if plan.node_configs[nid]["component"] == "orpheus.builtin.embed_out"
        ]
        lines = [
            '/* platform_io.c —— 嵌入 I/O 适配模板（自动生成，可按实际硬件修改）。',
            ' * 用户只需实现三个函数：',
            ' *   orpheus_platform_io_init()      ：一次性初始化（配置 DMA/编解码器）；',
            ' *   orpheus_platform_io_pre_block() ：每块处理前，把采集数据写入 g_embed_in_* 并设置 src_frames；',
            ' *   orpheus_platform_io_post_block()：每块处理后，把 g_embed_out_* 交给 DAC。',
            ' * 重新生成工程会覆盖本文件，请另存副本或生成后手工合并。',
            ' */',
            '#include <string.h>',
            '#include "orpheus_abi.h"',
        ]
        if in_nodes:
            lines.append('#include "orpheus_embed_in.h"')
        if out_nodes:
            lines.append('#include "orpheus_embed_out.h"')
        lines.append('')
        for nid in in_nodes:
            s = self._sanitized_node_id(nid)
            lines.append(f'extern float g_embed_in_{s}[];')
            lines.append(f'extern EmbedInState* orpheus_embed_in_state_{s}(void);')
        for nid in out_nodes:
            s = self._sanitized_node_id(nid)
            lines.append(f'extern float g_embed_out_{s}[];')
            lines.append(f'extern EmbedOutState* orpheus_embed_out_state_{s}(void);')
        lines.append('')
        lines.append('void orpheus_platform_io_init(void) {')
        lines.append('    /* USER CODE BEGIN */')
        lines.append('    /* 例：配置 DMA/编解码器，挂中断。输入数据也可在中断里直接写 g_embed_in_* */')
        lines.append('    /* USER CODE END */')
        lines.append('}')
        lines.append('')
        lines.append('void orpheus_platform_io_pre_block(void) {')
        for nid in in_nodes:
            s = self._sanitized_node_id(nid)
            cfg = plan.node_configs[nid]
            frames = cfg.get("frames") or plan.block_size
            lines.append(f'    EmbedInState* in = orpheus_embed_in_state_{s}();')
            lines.append(
                f'    /* 例：memcpy(g_embed_in_{s}, dma_rx, {frames} * in->channels * sizeof(float)); */'
            )
            lines.append(f'    /*      in->src_frames = {frames}; 不足一帧会补零并累计欠载 */')
            lines.append('    (void)in;')
        lines.append('    /* USER CODE BEGIN */')
        lines.append('    /* USER CODE END */')
        lines.append('}')
        lines.append('')
        lines.append('void orpheus_platform_io_post_block(void) {')
        for nid in out_nodes:
            s = self._sanitized_node_id(nid)
            cfg = plan.node_configs[nid]
            frames = cfg.get("frames") or plan.block_size
            lines.append(f'    EmbedOutState* out = orpheus_embed_out_state_{s}();')
            lines.append(f'    /* 例：dac_write(out->dst, {frames} * out->channels); */')
            lines.append('    (void)out;')
        lines.append('    /* USER CODE BEGIN */')
        lines.append('    /* USER CODE END */')
        lines.append('}')
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))

    def _generate_ids(
        self,
        plan: ExecutionPlan,
        include_dir: Path,
        src_dir: Path,
        output_dir: Path,
    ) -> None:
        """生成 32 位数据 ID 宏、ID map（类型/长度/偏移）与可读内存布局。

        产物：
        - include/orpheus_arena.h  模块嵌套结构体（子模块实例=一块连续内存）+ OrpheusArena；
        - include/orpheus_ids.h    ORPHEUS_<KIND>_<模块><参数> 宏 + CHAR_COUNT；
        - include/orpheus_id_map.h / src/orpheus_id_map.c  静态 ID map（offsetof/sizeof 精确偏移）；
        - memory_map.md            可读布局（对照 id_map.c 即可完全得知内存布局）。
        """
        modules = {m["path"]: m for m in plan.modules}
        ordered_paths = [m["path"] for m in plan.modules]
        state_nodes = {nid for nid in plan.execution_order if self._state_type(nid, plan)}

        # ---------------- include/orpheus_arena.h ----------------
        arena_lines = [
            '#ifndef ORPHEUS_ARENA_H',
            '#define ORPHEUS_ARENA_H',
            '#include "orpheus_abi.h"',
            '/* 模块内存布局：每个子模块实例 = 一块连续内存（嵌套结构体，布局由 C 编译器决定）。',
            ' * flatten 只决定执行拓扑；此处按模块递归嵌套，叶子状态按执行序排列。 */',
        ]
        for cid in sorted({plan.node_configs[n]["component"] for n in plan.execution_order}):
            arena_lines.append(f'#include "orpheus_{cid.replace("orpheus.builtin.", "")}.h"')
        arena_lines.append('')
        for path in reversed(ordered_paths):
            if not path:
                continue
            mod = modules[path]
            arena_lines.append(f'typedef struct {{')
            for leaf in mod.get("leaves", []):
                st = self._state_type(leaf["node"], plan)
                if st:
                    arena_lines.append(
                        f'    {st} {self._sanitized_node_id(leaf["node"].split("__")[-1])};'
                    )
            for child in self._module_children(modules, path):
                arena_lines.append(
                    f'    {self._module_type_name(child)} {self._module_member(child)};'
                )
            arena_lines.append(f'}} {self._module_type_name(path)};')
            arena_lines.append('')
        root = modules.get("", {})
        arena_lines.append('typedef struct {')
        for leaf in root.get("leaves", []):
            st = self._state_type(leaf["node"], plan)
            if st:
                arena_lines.append(
                    f'    {st} {self._sanitized_node_id(leaf["node"].split("__")[-1])};'
                )
        for child in self._module_children(modules, ""):
            arena_lines.append(
                f'    {self._module_type_name(child)} {self._module_member(child)};'
            )
        arena_lines.append('} OrpheusArena;')
        arena_lines.append('')
        arena_lines.append('#endif /* ORPHEUS_ARENA_H */')
        (include_dir / "orpheus_arena.h").write_text(
            "\n".join(arena_lines) + "\n", encoding="utf-8"
        )

        # ---------------- include/orpheus_ids.h ----------------
        ids_lines = [
            '#ifndef ORPHEUS_IDS_H',
            '#define ORPHEUS_IDS_H',
            '#include <stddef.h>',
            '#include <stdint.h>',
            '#include "orpheus_abi.h"',
            '/* 数据 ID（32 位宏）：单 ID 寻址，方向只在接口（orpheus_data_read/write）。',
            ' * ID = kind<<28 | module_id<<16 | slot；kind 0x0..0xF：',
            ' * TUNE/CMD/PROBE/BULK/STATE/MODULE/CUSTOM，其余 Reserved（见 OrpheusIdKind）。',
            ' * CUSTOM 类显式保留给用户自定义资源（可自行分配该类 ID 空间）。 */',
            '',
        ]
        for path in ordered_paths:
            if not path:
                continue
            mod = modules[path]
            ids_lines.append(
                f'#define ORPHEUS_MODULE_{self._camel(path)} '
                f'(ORPHEUS_ID_MAKE(ORPHEUS_ID_MODULE, {mod["id"]}, 0))'
            )
        ids_lines.append('')
        for path in ordered_paths:
            mod = modules[path]
            for slot, p in enumerate(self._module_data_points(plan, mod)):
                kind = self._point_kind(p)
                name = self._point_macro_name(mod, p)
                ctype = self._ctype_of(p.get("type", "float"))
                count = int(p.get("count", 1) or 1)
                ids_lines.append(
                    f'#define ORPHEUS_{kind}_{name} '
                    f'(ORPHEUS_ID_MAKE(ORPHEUS_ID_{kind}, {mod["id"]}, {slot}))'
                )
                ids_lines.append(
                    f'#define ORPHEUS_CHAR_COUNT_{name} (sizeof({ctype}) * {count}U)'
                )
        ids_lines.append('')
        ids_lines.append('#endif /* ORPHEUS_IDS_H */')
        (include_dir / "orpheus_ids.h").write_text(
            "\n".join(ids_lines) + "\n", encoding="utf-8"
        )

        # ---------------- include/orpheus_id_map.h / src/orpheus_id_map.c ----------------
        map_h = [
            '#ifndef ORPHEUS_ID_MAP_H',
            '#define ORPHEUS_ID_MAP_H',
            '#include <stddef.h>',
            '#include <stdint.h>',
            '#include "orpheus_abi.h"',
            '/* 数据 ID map：ID → 类型/长度/偏移。与 memory_map.md 对照即可完全得知内存布局：',
            ' *   arena 基址 + arena_offset = 叶子状态内存，字节数见 byte_size；',
            ' *   参数在叶子状态内的精确偏移由运行时注册表（register_slots）给出。 */',
            'typedef struct {',
            '    uint32_t id;',
            '    const char* name;       /* 中文显示名 */',
            '    uint32_t kind;          /* OrpheusIdKind */',
            '    uint32_t type;          /* OrpheusValueType */',
            '    uint32_t count;',
            '    size_t byte_size;       /* count × sizeof(type) */',
            '    uint32_t module_id;',
            '    uint32_t slot;',
            '    size_t module_offset;   /* 模块在 arena 中的偏移（offsetof） */',
            '    size_t arena_offset;    /* 叶子状态在 arena 中的完整偏移（offsetof） */',
            '} OrpheusIdEntry;',
            'const OrpheusIdEntry* orpheus_id_map(size_t* out_count);',
            '#endif /* ORPHEUS_ID_MAP_H */',
        ]
        (include_dir / "orpheus_id_map.h").write_text(
            "\n".join(map_h) + "\n", encoding="utf-8"
        )

        map_c = [
            '#include "orpheus_id_map.h"',
            '#include "orpheus_ids.h"',
            '#include "orpheus_arena.h"',
            '',
            'static const OrpheusIdEntry g_id_map[] = {',
        ]
        for path in ordered_paths:
            if not path:
                continue
            mod = modules[path]
            chain = ".".join(self._sanitized_node_id(seg) for seg in path.split("__"))
            mod_type = self._module_type_name(path)
            map_c.append(
                f'    {{ ORPHEUS_MODULE_{self._camel(path)}, "{self._camel(path)}", '
                f'ORPHEUS_ID_MODULE, ORPHEUS_VALUE_BULK_REF, 1, sizeof({mod_type}), '
                f'{mod["id"]}, 0, offsetof(OrpheusArena, {chain}), '
                f'offsetof(OrpheusArena, {chain}) }},'
            )
        for path in ordered_paths:
            mod = modules[path]
            for slot, p in enumerate(self._module_data_points(plan, mod)):
                nid = p["node"]
                if self._state_type(nid, plan) is None:
                    continue
                kind = self._point_kind(p)
                name = self._point_macro_name(mod, p)
                ctype = self._ctype_of(p.get("type", "float"))
                vtype = self._value_type_of(p.get("type", "float"))
                count = int(p.get("count", 1) or 1)
                chain = self._arena_member_chain(nid)
                segs = chain.split(".")
                mod_chain = ".".join(segs[:-1])
                mod_off = f'offsetof(OrpheusArena, {mod_chain})' if mod_chain else '0'
                arena_off = f'offsetof(OrpheusArena, {chain})'
                display = p.get("name", p["id"]).replace('"', '\\"')
                map_c.append(
                    f'    {{ ORPHEUS_{kind}_{name}, "{display}", ORPHEUS_ID_{kind}, '
                    f'{vtype}, {count}, sizeof({ctype}) * {count}U, '
                    f'{mod["id"]}, {slot}, {mod_off}, {arena_off} }},'
                )
        map_c.append('};')
        map_c.append('')
        map_c.append('const OrpheusIdEntry* orpheus_id_map(size_t* out_count) {')
        map_c.append('    if (out_count) *out_count = sizeof(g_id_map) / sizeof(g_id_map[0]);')
        map_c.append('    return g_id_map;')
        map_c.append('}')
        (src_dir / "orpheus_id_map.c").write_text(
            "\n".join(map_c) + "\n", encoding="utf-8"
        )

        # ---------------- memory_map.md（可读布局） ----------------
        md_lines = [
            '# Orpheus 数据 ID 与内存布局（生成期静态视图）',
            '',
            f'- 采样率 {plan.sample_rate} Hz / 块长 {plan.block_size} 帧',
            '- 精确偏移见 `src/orpheus_id_map.c`（编译期 offsetof/sizeof）；运行时 `RESOLVE <id>` 给出注册表最终地址。',
            '- kind：TUNE/CMD/PROBE/BULK/STATE/MODULE/CUSTOM，其余 Reserved（`OrpheusIdKind`）。',
            '',
            '## 模块（MODULE 类 ID，指向整块连续内存）',
            '',
        ]
        for path in ordered_paths:
            if not path:
                continue
            mod = modules[path]
            value = self._id_value("MODULE", mod["id"], 0)
            md_lines.append(
                f'- `ORPHEUS_MODULE_{self._camel(path)}` = 0x{value:08X}：'
                f'`{path}`，`sizeof({self._module_type_name(path)})` 字节'
            )
        md_lines.append('')
        md_lines.append('## 数据点（ID 宏 / 类别 / 类型 × 个数 = 字节数）')
        md_lines.append('')
        for path in ordered_paths:
            mod = modules[path]
            md_lines.append(f'### 模块 `{path or "<顶层>"}` (id={mod["id"]})')
            md_lines.append('')
            for slot, p in enumerate(self._module_data_points(plan, mod)):
                kind = self._point_kind(p)
                name = self._point_macro_name(mod, p)
                ctype = self._ctype_of(p.get("type", "float"))
                count = int(p.get("count", 1) or 1)
                display = p.get("name", p["id"])
                runtime = ' [运行期槽]' if p.get("runtime") else ''
                value = self._id_value(kind, mod["id"], slot)
                md_lines.append(
                    f'- `ORPHEUS_{kind}_{name}` = 0x{value:08X}：{display}{runtime}，'
                    f'{ctype} × {count} = {self._bytes_of(ctype) * count} B'
                )
            md_lines.append('')
        (output_dir / "memory_map.md").write_text(
            "\n".join(md_lines), encoding="utf-8"
        )

    def _generate_cmake(self, plan: ExecutionPlan, component_ids: list[str], output_dir: Path) -> None:
        lines: list[str] = []
        lines.append('cmake_minimum_required(VERSION 3.16)')
        lines.append('project(orpheus_generated C)')
        lines.append('set(CMAKE_C_STANDARD 11)')
        lines.append('set(CMAKE_C_STANDARD_REQUIRED ON)')
        lines.append("")
        lines.append('include_directories(${CMAKE_SOURCE_DIR}/include)')
        # 依赖闭包组件的 include 目录全局可见（父组件头文件可 include 子组件头）
        for cid in component_ids:
            comp_target = self._component_target_name(cid)
            lines.append(f'include_directories(${{CMAKE_SOURCE_DIR}}/components/{comp_target}/include)')
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

        app_sources = "src/main.c"
        if (output_dir / "src" / "platform_io.c").exists():
            app_sources += " src/platform_io.c"
        if (output_dir / "src" / "orpheus_id_map.c").exists():
            app_sources += " src/orpheus_id_map.c"
        lines.append(f'add_executable(orpheus_generated_app {app_sources})')
        libs = " ".join(self._component_target_name(cid) for cid in component_ids)
        lines.append(f'target_link_libraries(orpheus_generated_app {libs})')

        with open(output_dir / "CMakeLists.txt", "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
