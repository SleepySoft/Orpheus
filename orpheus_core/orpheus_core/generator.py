"""Code generator: produce a standalone C/C++ project from an Execution Plan."""

from __future__ import annotations

import json
import re
import shutil
from pathlib import Path
from typing import Any

from orpheus_core.compiler import ExecutionPlan
from orpheus_core.parameter_catalog import ID_SLOT_MODULE, id_value
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

    def _ctype_of(self, ptype: str) -> str:
        return {"float": "float", "int": "int32_t", "bool": "bool",
                "string": "const char*"}.get(ptype, "float")

    def _value_type_of(self, ptype: str) -> str:
        return {"float": "ORPHEUS_VALUE_FLOAT", "int": "ORPHEUS_VALUE_INT",
                "bool": "ORPHEUS_VALUE_BOOL",
                "string": "ORPHEUS_VALUE_STRING"}.get(ptype, "ORPHEUS_VALUE_FLOAT")

    def _bytes_of(self, ctype: str) -> int:
        return {"float": 4, "int32_t": 4, "bool": 1, "const char*": 8}.get(ctype, 4)

    def _id_map_by_module(self, plan: ExecutionPlan) -> dict[int, list[dict]]:
        """plan.id_map 按模块 id 分组（保持 id_map 顺序 = 模块内槽顺序）。"""
        by_module: dict[int, list[dict]] = {}
        for entry in plan.id_map:
            by_module.setdefault((entry["id"] >> 16) & 0xFF, []).append(entry)
        return by_module

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
            if (info.root_dir / "user").exists():
                shutil.copytree(info.root_dir / "user", comp_out / "user", dirs_exist_ok=True)
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

        # 声明式平台节点（platform_hook）：生成 init/read/write 钩子，用户程序引用
        if plan.declarations:
            self._generate_platform_hooks(
                plan, src_dir / "platform_hooks.c", include_dir / "orpheus_platform_hooks.h"
            )

        # 数据 ID（32 位宏）+ ID map + 内存布局（模块嵌套 arena 定义在 include/orpheus_arena.h）
        self._generate_ids(plan, include_dir, src_dir, output_dir)

        # 生成路径控制 API + BULK 双 bank（可选：仅工程 double_bank 生效的槽产出影子）
        self._generate_control(plan, include_dir, src_dir)

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
        lines.append('#include "orpheus_control.h"')
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
        lines.append('    orpheus_control_slot_register(ctx, info);')
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
            lines.append(f'        g_reg.ctx = g_state_{s};')
            lines.append(f'        orpheus_control_set_reg_node("{node_id}", g_state_{s});')
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
        lines.append('    orpheus_control_commit_bulk();')
        lines.append('')
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

        # hex 工具（--msg 二进制消息 CLI）
        lines.append('static const char* orpheus_hex_digits = "0123456789abcdef";')
        lines.append('static int orpheus_from_hex(const char* hx, uint8_t* out, size_t* out_len) {')
        lines.append('    size_t n = strlen(hx);')
        lines.append('    if (n % 2 != 0) return -1;')
        lines.append('    *out_len = 0;')
        lines.append('    for (size_t i = 0; i < n; i += 2) {')
        lines.append('        int hi = -1, lo = -1;')
        lines.append('        char c1 = hx[i], c2 = hx[i + 1];')
        lines.append('        if (c1 >= \'0\' && c1 <= \'9\') hi = c1 - \'0\';')
        lines.append('        else if (c1 >= \'a\' && c1 <= \'f\') hi = c1 - \'a\' + 10;')
        lines.append('        else if (c1 >= \'A\' && c1 <= \'F\') hi = c1 - \'A\' + 10;')
        lines.append('        if (c2 >= \'0\' && c2 <= \'9\') lo = c2 - \'0\';')
        lines.append('        else if (c2 >= \'a\' && c2 <= \'f\') lo = c2 - \'a\' + 10;')
        lines.append('        else if (c2 >= \'A\' && c2 <= \'F\') lo = c2 - \'A\' + 10;')
        lines.append('        if (hi < 0 || lo < 0) return -1;')
        lines.append('        out[(*out_len)++] = (uint8_t)((hi << 4) | lo);')
        lines.append('    }')
        lines.append('    return 0;')
        lines.append('}')
        lines.append('')
        lines.append('/* 测试/部署用 echo hook：请求 payload 原样返回（CUSTOM 消息路径验证）。 */')
        lines.append('static int orpheus_echo_hook(void* ctx, uint32_t id, uint32_t event,')
        lines.append('                            const OrpheusBlob* req, OrpheusBlob* resp) {')
        lines.append('    (void)ctx; (void)id; (void)event;')
        lines.append('    if (resp == NULL) return ORPHEUS_HOOK_HANDLED;')
        lines.append('    if (req != NULL && req->len > 0) { memcpy((void*)resp->data, req->data, req->len); resp->len = req->len; }')
        lines.append('    else resp->len = 0;')
        lines.append('    return ORPHEUS_HOOK_HANDLED;')
        lines.append('}')
        lines.append('')
        lines.append('// Main stub: argv[1] = number of blocks to process (default 1000)')
        lines.append('int main(int argc, char** argv) {')
        lines.append('    int blocks = argc > 1 ? atoi(argv[1]) : 1000;')
        lines.append(f'    int rc = orpheus_generated_init({plan.sample_rate}, {plan.block_size});')
        lines.append('    if (rc != ORPHEUS_OK) {')
        lines.append('        fprintf(stderr, "init failed: %d\\n", rc);')
        lines.append('        return 1;')
        lines.append('    }')
        lines.append('    /* 可选控制参数（双 bank / BULK 读写，部署与验证用）：')
        lines.append('       --write-bulk <node> <key> <n> <v0>... | --write-bulk-id <id> <n> <v0>...')
        lines.append('       --run <blocks>（控制模式下处理 N 块触发块边界提交）')
        lines.append('       --read-bulk <node> <key> | --read-bulk-id <id> */')
        lines.append('    int control_mode = 0;')
        lines.append('    int run_blocks = 0;')
        lines.append('    const char* rb_node = NULL;')
        lines.append('    const char* rb_key = NULL;')
        lines.append('    int rb_by_id = 0;')
        lines.append('    uint32_t rb_id = 0;')
        lines.append('    const char* msg_list[16];')
        lines.append('    int msg_count = 0;')
        lines.append('    for (int i = 2; i < argc; ++i) {')
        lines.append('        if (strcmp(argv[i], "--write-bulk") == 0 && i + 3 < argc) {')
        lines.append('            const char* node = argv[i + 1];')
        lines.append('            const char* key = argv[i + 2];')
        lines.append('            size_t n = (size_t)atoi(argv[i + 3]);')
        lines.append('            float vals[64]; size_t got = 0;')
        lines.append('            for (size_t k = 0; k < n && i + 4 + (int)k < argc && got < 64; ++k)')
        lines.append('                vals[got++] = (float)atof(argv[i + 4 + (int)k]);')
        lines.append('            i += 3 + (int)n;')
        lines.append('            int r = orpheus_control_write_bulk(node, key, vals, got);')
        lines.append('            printf("%s WRITEBULK %s %s\\n", r == 0 ? "OK" : "ERR", node, key);')
        lines.append('            control_mode = 1;')
        lines.append('        } else if (strcmp(argv[i], "--write-bulk-id") == 0 && i + 3 < argc) {')
        lines.append('            uint32_t id = (uint32_t)strtoul(argv[i + 1], NULL, 0);')
        lines.append('            size_t n = (size_t)atoi(argv[i + 2]);')
        lines.append('            float vals[64]; size_t got = 0;')
        lines.append('            for (size_t k = 0; k < n && i + 3 + (int)k < argc && got < 64; ++k)')
        lines.append('                vals[got++] = (float)atof(argv[i + 3 + (int)k]);')
        lines.append('            i += 2 + (int)n;')
        lines.append('            int r = orpheus_control_write_bulk_id(id, vals, got);')
        lines.append('            printf("%s WRITEBULK 0x%08X\\n", r == 0 ? "OK" : "ERR", id);')
        lines.append('            control_mode = 1;')
        lines.append('        } else if (strcmp(argv[i], "--run") == 0 && i + 1 < argc) {')
        lines.append('            run_blocks = atoi(argv[++i]);')
        lines.append('        } else if (strcmp(argv[i], "--read-bulk") == 0 && i + 2 < argc) {')
        lines.append('            rb_node = argv[i + 1];')
        lines.append('            rb_key = argv[i + 2];')
        lines.append('            i += 2;')
        lines.append('            control_mode = 1;')
        lines.append('        } else if (strcmp(argv[i], "--read-bulk-id") == 0 && i + 1 < argc) {')
        lines.append('            rb_id = (uint32_t)strtoul(argv[i + 1], NULL, 0);')
        lines.append('            rb_by_id = 1;')
        lines.append('            i += 1;')
        lines.append('            control_mode = 1;')
        lines.append('        } else if (strcmp(argv[i], "--msg") == 0 && i + 1 < argc && msg_count < 16) {')
        lines.append('            msg_list[msg_count++] = argv[++i];')
        lines.append('            control_mode = 1;')
        lines.append('        } else if (strcmp(argv[i], "--echo-hook") == 0 && i + 1 < argc) {')
        lines.append('            uint32_t id = (uint32_t)strtoul(argv[++i], NULL, 0);')
        lines.append('            if (orpheus_control_register_hook(id, orpheus_echo_hook, NULL) != 0) {')
        lines.append('                printf("ERR ECHO-HOOK 0x%08X\\n", id);')
        lines.append('                return 1;')
        lines.append('            }')
        lines.append('            control_mode = 1;')
        lines.append('        }')
        lines.append('    }')
        lines.append('    if (control_mode) {')
        lines.append('        for (int i = 0; i < run_blocks; ++i) {')
        lines.append(f'            if (orpheus_generated_process({plan.block_size}) != ORPHEUS_OK) return 1;')
        lines.append('        }')
        lines.append('        for (int m = 0; m < msg_count; ++m) {')
        lines.append('            uint8_t in[4096]; size_t in_len = 0;')
        lines.append('            uint8_t out[4096]; size_t out_len = 0;')
        lines.append('            if (orpheus_from_hex(msg_list[m], in, &in_len) != 0) {')
        lines.append('                printf("ERR MSG hex\\n");')
        lines.append('                continue;')
        lines.append('            }')
        lines.append('            if (orpheus_control_message(in, in_len, out, sizeof(out), &out_len) != 0) {')
        lines.append('                printf("ERR MSG dispatch\\n");')
        lines.append('                continue;')
        lines.append('            }')
        lines.append('            if (out_len == 0) { printf("MSGNONE\\n"); continue; }')
        lines.append('            printf("MSGRSP ");')
        lines.append('            for (size_t k = 0; k < out_len; ++k) {')
        lines.append('                printf("%c%c", orpheus_hex_digits[out[k] >> 4], orpheus_hex_digits[out[k] & 0xF]);')
        lines.append('            }')
        lines.append('            printf("\\n");')
        lines.append('        }')
        lines.append('        /* 读回：解析时仅记录，处理完再读（块边界提交后才是新值） */')
        lines.append('        if (rb_node) {')
        lines.append('            size_t n = orpheus_control_bulk_count(rb_node, rb_key);')
        lines.append('            if (n == 0 || n > 64) { printf("ERR GETBULK %s %s\\n", rb_node, rb_key); goto teardown; }')
        lines.append('            float vals[64];')
        lines.append('            if (orpheus_control_get_bulk(rb_node, rb_key, vals, n) == 0) {')
        lines.append('                printf("BULKVALUE %s %s", rb_node, rb_key);')
        lines.append('                for (size_t k = 0; k < n; ++k) printf(" %g", vals[k]);')
        lines.append('                printf("\\n");')
        lines.append('            } else { printf("ERR GETBULK %s %s\\n", rb_node, rb_key); }')
        lines.append('        } else if (rb_by_id) {')
        lines.append('            size_t n = orpheus_control_bulk_count_id(rb_id);')
        lines.append('            if (n == 0 || n > 64) { printf("ERR GETBULK 0x%08X\\n", rb_id); goto teardown; }')
        lines.append('            float vals[64];')
        lines.append('            if (orpheus_control_get_bulk_id(rb_id, vals, n) == 0) {')
        lines.append('                printf("BULKVALUE 0x%08X", rb_id);')
        lines.append('                for (size_t k = 0; k < n; ++k) printf(" %g", vals[k]);')
        lines.append('                printf("\\n");')
        lines.append('            } else { printf("ERR GETBULK 0x%08X\\n", rb_id); }')
        lines.append('        }')
        lines.append('        goto teardown;')
        lines.append('    }')
        lines.append('    for (int i = 0; i < blocks; ++i) {')
        lines.append(f'        rc = orpheus_generated_process({plan.block_size});')
        lines.append('        if (rc != ORPHEUS_OK) return 1;')
        lines.append('    }')
        lines.append('teardown:')
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

    def _generate_platform_hooks(self, plan: ExecutionPlan, src_path: Path, hdr_path: Path) -> None:
        """声明式平台节点（platform_hook）→ init/read/write 钩子（USER CODE 填充）。

        生成：
        - include/orpheus_platform_hooks.h：每个钩子的函数声明（orpheus_platform_<name>_*）；
        - src/platform_hooks.c：空实现 + USER CODE BEGIN/END 段，用户按硬件填充。
        """
        hooks = [
            d for d in plan.declarations
            if d["component"] == "orpheus.builtin.platform_hook"
        ]
        if not hooks:
            return

        def sym(d: dict[str, Any]) -> str:
            name = str(d["params"].get("hook_name") or d["id"])
            return self._sanitized_node_id(name)

        hdr = [
            '#ifndef ORPHEUS_PLATFORM_HOOKS_H',
            '#define ORPHEUS_PLATFORM_HOOKS_H',
            '#include <stdint.h>',
            '#ifdef __cplusplus',
            'extern "C" {',
            '#endif',
            '',
        ]
        src = [
            '/* platform_hooks.c —— 平台资源钩子（自动生成，USER CODE 段可按硬件填充）。',
            ' * 供用户程序引用：include "orpheus_platform_hooks.h"，在 USER CODE 段实现各钩子',
            ' * （如 amixer 控件、通信收发、传感器读取）。重新生成会覆盖本文件，请另存副本。 */',
            '#include "orpheus_platform_hooks.h"',
            '',
        ]
        for d in hooks:
            s = sym(d)
            iface = str(d["params"].get("interface") or "generic")
            note = str(d["params"].get("note") or "")
            hdr.append(f'/* 节点 {d["id"]} · interface={iface}{" · " + note if note else ""} */')
            hdr.append(f'void orpheus_platform_{s}_init(void);')
            hdr.append(f'int orpheus_platform_{s}_read(float* value);')
            hdr.append(f'int orpheus_platform_{s}_write(float value);')
            hdr.append('')
            src.append(f'/* 节点 {d["id"]} · interface={iface}{" · " + note if note else ""} */')
            src.append(f'void orpheus_platform_{s}_init(void) {{')
            src.append('    /* USER CODE BEGIN init */')
            src.append('    /* 例（amixer）：snd_mixer_open / snd_mixer_selem_id_set_name */')
            src.append('    /* USER CODE END init */')
            src.append('}')
            src.append(f'int orpheus_platform_{s}_read(float* value) {{')
            src.append('    /* USER CODE BEGIN read */')
            src.append('    *value = 0.0f;')
            src.append('    /* USER CODE END read */')
            src.append('    return 0;')
            src.append('}')
            src.append(f'int orpheus_platform_{s}_write(float value) {{')
            src.append('    /* USER CODE BEGIN write */')
            src.append('    (void)value;')
            src.append('    /* USER CODE END write */')
            src.append('    return 0;')
            src.append('}')
            src.append('')
        hdr += [
            '#ifdef __cplusplus',
            '}',
            '#endif',
            '',
            '#endif /* ORPHEUS_PLATFORM_HOOKS_H */',
            '',
        ]
        hdr_path.write_text("\n".join(hdr), encoding="utf-8")
        src_path.write_text("\n".join(src), encoding="utf-8")

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
            ' * RTC/TUNE/PROBE/STATE/CUSTOM，其余 Reserved（见 OrpheusIdKind）。',
            ' * 形式（SCALAR/BULK/MODULE）是独立维度，由 ID map 与 CHAR_COUNT 描述。 */',
            '',
        ]
        for path in ordered_paths:
            if not path:
                continue
            mod = modules[path]
            ids_lines.append(
                f'#define ORPHEUS_MODULE_{self._camel(path)} '
                f'(ORPHEUS_ID_MAKE(ORPHEUS_ID_TUNE, {mod["id"]}, ORPHEUS_ID_SLOT_MODULE))'
            )
        ids_lines.append('')
        by_module = self._id_map_by_module(plan)
        for path in ordered_paths:
            mod = modules[path]
            for entry in by_module.get(mod["id"], []):
                kind = entry["kind"]
                name = self._point_macro_name(mod, {"id": entry["key"], "node": entry["node"]})
                ctype = self._ctype_of(entry["type"])
                count = entry["count"]
                ids_lines.append(
                    f'#define ORPHEUS_{kind}_{name} '
                    f'(ORPHEUS_ID_MAKE(ORPHEUS_ID_{kind}, {mod["id"]}, {entry["id"] & 0xFFFF}))'
                )
                if kind != "CUSTOM":  # 消息入口无内存尺寸
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
            '    uint32_t form;          /* OrpheusDataForm（标量/bulk/模块包，与用途正交） */',
            '    uint32_t double_bank;   /* BULK 双 bank 是否生效（工程 auto/on/off × 组件声明） */',
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
                f'ORPHEUS_ID_TUNE, ORPHEUS_FORM_MODULE, 0, ORPHEUS_VALUE_BULK_REF, '
                f'1, sizeof({mod_type}), '
                f'{mod["id"]}, ORPHEUS_ID_SLOT_MODULE, offsetof(OrpheusArena, {chain}), '
                f'offsetof(OrpheusArena, {chain}) }},'
            )
        by_module = self._id_map_by_module(plan)
        for path in ordered_paths:
            mod = modules[path]
            for entry in by_module.get(mod["id"], []):
                nid = entry["node"]
                if self._state_type(nid, plan) is None:
                    continue
                kind = entry["kind"]
                if kind == "CUSTOM":
                    continue  # 消息入口无内存，不产生 map 偏移项
                name = self._point_macro_name(mod, {"id": entry["key"], "node": nid})
                ctype = self._ctype_of(entry["type"])
                vtype = self._value_type_of(entry["type"])
                count = entry["count"]
                chain = self._arena_member_chain(nid)
                segs = chain.split(".")
                mod_chain = ".".join(segs[:-1])
                mod_off = f'offsetof(OrpheusArena, {mod_chain})' if mod_chain else '0'
                arena_off = f'offsetof(OrpheusArena, {chain})'
                display = entry.get("name", entry["key"]).replace('"', '\\"')
                map_c.append(
                    f'    {{ ORPHEUS_{kind}_{name}, "{display}", ORPHEUS_ID_{kind}, '
                    f'ORPHEUS_FORM_{entry["form"]}, '
                    f'{"1" if entry.get("double_bank") else "0"}, {vtype}, {count}, '
                    f'sizeof({ctype}) * {count}U, '
                    f'{mod["id"]}, {entry["id"] & 0xFFFF}, {mod_off}, {arena_off} }},'
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
            '- kind：RTC/TUNE/PROBE/STATE/CUSTOM（RTC 最高频，排第一），其余 Reserved；'
            '形式（SCALAR/BULK/MODULE）是独立维度。',
            '',
            '## 模块包（用途=TUNE，形式=MODULE，指向整块连续内存）',
            '',
        ]
        for path in ordered_paths:
            if not path:
                continue
            mod = modules[path]
            value = id_value("TUNE", mod["id"], ID_SLOT_MODULE)
            md_lines.append(
                f'- `ORPHEUS_MODULE_{self._camel(path)}` = 0x{value:08X}：'
                f'`{path}`（用途=TUNE，形式=模块包），`sizeof({self._module_type_name(path)})` 字节'
            )
        md_lines.append('')
        md_lines.append('## 数据点（ID 宏 / 类别 / 类型 × 个数 = 字节数）')
        md_lines.append('')
        for path in ordered_paths:
            mod = modules[path]
            md_lines.append(f'### 模块 `{path or "<顶层>"}` (id={mod["id"]})')
            md_lines.append('')
            for entry in by_module.get(mod["id"], []):
                kind = entry["kind"]
                name = self._point_macro_name(
                    mod, {"id": entry["key"], "node": entry["node"]}
                )
                ctype = self._ctype_of(entry["type"])
                count = entry["count"]
                display = entry.get("name", entry["key"])
                runtime = ' [运行期槽]' if entry.get("runtime") else ''
                value = entry["id"]
                if kind == "CUSTOM":
                    reply = "response" if entry.get("reply") else "notification"
                    md_lines.append(
                        f'- `ORPHEUS_{kind}_{name}` = 0x{value:08X}：{display}（消息入口，{reply}）'
                    )
                    continue
                form = entry["form"].lower()
                db = "，双缓冲" if entry.get("double_bank") else ""
                md_lines.append(
                    f'- `ORPHEUS_{kind}_{name}` = 0x{value:08X}：{display}{runtime}，'
                    f'形式={form}{db}，{ctype} × {count} = {self._bytes_of(ctype) * count} B'
                )
            md_lines.append('')
        (output_dir / "memory_map.md").write_text(
            "\n".join(md_lines), encoding="utf-8"
        )

    def _generate_control(self, plan: ExecutionPlan, include_dir: Path, src_dir: Path) -> None:
        """生成路径控制 API + BULK 双 bank（可选）。

        src/orpheus_control.c：
        - 影子数组（仅 plan.id_map 中 double_bank 生效的 BULK 槽；off 时为空，零额外内存）；
        - 槽表（init 时 register_slots 记录，write/get 按 node/key 寻址）；
        - orpheus_control_write_bulk/get_bulk（node/key 与按 ID 两套）；
        - orpheus_control_commit_bulk()：块边界把 pending 影子 memcpy 提交到 active。
        """
        db_entries = [e for e in plan.id_map if e.get("form") == "BULK" and e.get("double_bank")]
        bulk_entries = [e for e in plan.id_map if e.get("form") == "BULK"]
        max_slots = len(plan.id_map) + 16

        h = [
            '#ifndef ORPHEUS_CONTROL_H',
            '#define ORPHEUS_CONTROL_H',
            '#include "orpheus_abi.h"',
            '#include <stddef.h>',
            '#include <stdint.h>',
            '/* 生成路径控制 API：按 node/key 或 32 位数据 ID 读写 BULK 槽。',
            ' * BULK 双 bank（可选）：工程 double_bank=auto/on 且组件声明时，写影子、块边界提交；',
            ' * 未开启（double_bank=off）直写 active 即时生效（部署省内存）。',
            ' * 常规无毛刺调音惯例：mute → 更新系数 → unmute；双 bank 仅用于必须边跑边更的少数场景。 */',
            'void orpheus_control_set_reg_node(const char* node, void* state);',
            'void orpheus_control_slot_register(void* state, const OrpheusSlotInfo* info);',
            'void orpheus_control_commit_bulk(void);',
            'size_t orpheus_control_bulk_count(const char* node, const char* key);',
            'size_t orpheus_control_bulk_count_id(uint32_t id);',
            'int orpheus_control_write_bulk(const char* node, const char* key, const void* data, size_t count);',
            'int orpheus_control_write_bulk_id(uint32_t id, const void* data, size_t count);',
            'int orpheus_control_get_bulk(const char* node, const char* key, void* out, size_t count);',
            'int orpheus_control_get_bulk_id(uint32_t id, void* out, size_t count);',
            'int orpheus_control_register_hook(uint32_t id, OrpheusHookFn fn, void* ctx);',
            'int orpheus_control_message(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap, size_t* out_len);',
            '#endif /* ORPHEUS_CONTROL_H */',
        ]
        (include_dir / "orpheus_control.h").write_text("\n".join(h) + "\n", encoding="utf-8")

        def shadow_var(e: dict) -> str:
            return f'g_db_{self._sanitized_node_id(e["node"])}_{self._sanitized_node_id(e["key"])}'

        c = [
            '#include "orpheus_control.h"',
            '#include <string.h>',
            '',
            f'#define ORPHEUS_GEN_MAX_SLOTS {max_slots}u',
            '',
            '/* 双 bank 影子区（仅生效槽；double_bank=off 时为空，零额外内存） */',
        ]
        for e in db_entries:
            c.append(f'static {self._ctype_of(e["type"])} {shadow_var(e)}[{e["count"]}];')
        c.append('')
        c.append('/* 槽表：init 时 register_slots 记录，write/get 按 node/key 寻址 */')
        c.append('typedef struct {')
        c.append('    const char* node;')
        c.append('    const char* key;')
        c.append('    uint32_t kind;')
        c.append('    uint32_t type;')
        c.append('    uint32_t count;')
        c.append('    size_t size;')
        c.append('    size_t offset;')
        c.append('    void* state;')
        c.append('    void* shadow;   /* NULL = 未开启双 bank（直写 active） */')
        c.append('    uint32_t pending;')
        c.append('} OrpheusGenSlot;')
        c.append('')
        c.append('static OrpheusGenSlot g_gen_slots[ORPHEUS_GEN_MAX_SLOTS];')
        c.append('static size_t g_gen_slot_count = 0;')
        c.append('static const char* g_reg_node = NULL;')
        c.append('')
        c.append('void orpheus_control_set_reg_node(const char* node, void* state) {')
        c.append('    g_reg_node = node;')
        c.append('    (void)state;')
        c.append('}')
        c.append('')
        c.append('static void* orpheus_control_shadow_of(const char* node, const char* key) {')
        for e in db_entries:
            c.append(f'    if (strcmp(node, "{e["node"]}") == 0 && strcmp(key, "{e["key"]}") == 0) return {shadow_var(e)};')
        c.append('    return NULL;')
        c.append('}')
        c.append('')
        c.append('void orpheus_control_slot_register(void* state, const OrpheusSlotInfo* info) {')
        c.append('    if (!info || !info->key || g_gen_slot_count >= ORPHEUS_GEN_MAX_SLOTS) return;')
        c.append('    OrpheusGenSlot* s = &g_gen_slots[g_gen_slot_count++];')
        c.append('    s->node = g_reg_node;')
        c.append('    s->key = info->key;')
        c.append('    s->kind = info->kind;')
        c.append('    s->type = info->type;')
        c.append('    s->count = info->count;')
        c.append('    s->size = info->size;')
        c.append('    s->offset = info->offset;')
        c.append('    s->state = state;')
        c.append('    s->shadow = orpheus_control_shadow_of(g_reg_node, info->key);')
        c.append('    s->pending = 0;')
        c.append('}')
        c.append('')
        c.append('static OrpheusGenSlot* orpheus_control_find(const char* node, const char* key) {')
        c.append('    for (size_t i = 0; i < g_gen_slot_count; ++i) {')
        c.append('        OrpheusGenSlot* s = &g_gen_slots[i];')
        c.append('        if (s->state && s->node && s->key && strcmp(s->node, node) == 0 && strcmp(s->key, key) == 0) return s;')
        c.append('    }')
        c.append('    return NULL;')
        c.append('}')
        c.append('')
        c.append('size_t orpheus_control_bulk_count(const char* node, const char* key) {')
        c.append('    OrpheusGenSlot* s = orpheus_control_find(node, key);')
        c.append('    return s ? s->count : 0;')
        c.append('}')
        c.append('')
        c.append('int orpheus_control_write_bulk(const char* node, const char* key, const void* data, size_t count) {')
        c.append('    OrpheusGenSlot* s = orpheus_control_find(node, key);')
        c.append('    if (!s || s->kind != ORPHEUS_SLOT_BULK) return -1;')
        c.append('    if (count > s->count) return -1;')
        c.append('    size_t span = count * s->size;')
        c.append('    if (s->shadow) {')
        c.append('        memcpy(s->shadow, data, span);')
        c.append('        s->pending = 1;')
        c.append('    } else {')
        c.append('        memcpy((char*)s->state + s->offset, data, span);')
        c.append('    }')
        c.append('    return 0;')
        c.append('}')
        c.append('')
        c.append('int orpheus_control_get_bulk(const char* node, const char* key, void* out, size_t count) {')
        c.append('    OrpheusGenSlot* s = orpheus_control_find(node, key);')
        c.append('    if (!s || s->kind != ORPHEUS_SLOT_BULK || !out) return -1;')
        c.append('    if (count > s->count) return -1;')
        c.append('    memcpy(out, (const char*)s->state + s->offset, count * s->size);')
        c.append('    return 0;')
        c.append('}')
        c.append('')
        c.append('/* 块边界提交：把 pending 影子一次性 memcpy 到 active（单控制写者假设） */')
        c.append('void orpheus_control_commit_bulk(void) {')
        c.append('    for (size_t i = 0; i < g_gen_slot_count; ++i) {')
        c.append('        OrpheusGenSlot* s = &g_gen_slots[i];')
        c.append('        if (s->pending && s->shadow) {')
        c.append('            memcpy((char*)s->state + s->offset, s->shadow, s->count * s->size);')
        c.append('            s->pending = 0;')
        c.append('        }')
        c.append('    }')
        c.append('}')
        c.append('')
        c.append('static const struct { uint32_t id; const char* node; const char* key; } g_bulk_id_ref[] = {')
        if bulk_entries:
            for e in bulk_entries:
                c.append(f'    {{ 0x{e["id"]:08X}U, "{e["node"]}", "{e["key"]}" }},')
        else:
            c.append('    { 0U, "", "" },  /* 无 bulk 槽：哨兵（id 永不为 0） */')
        c.append('};')
        c.append('')
        c.append('size_t orpheus_control_bulk_count_id(uint32_t id) {')
        c.append('    for (size_t i = 0; i < sizeof(g_bulk_id_ref) / sizeof(g_bulk_id_ref[0]); ++i) {')
        c.append('        if (g_bulk_id_ref[i].id == id) return orpheus_control_bulk_count(g_bulk_id_ref[i].node, g_bulk_id_ref[i].key);')
        c.append('    }')
        c.append('    return 0;')
        c.append('}')
        c.append('')
        c.append('int orpheus_control_write_bulk_id(uint32_t id, const void* data, size_t count) {')
        c.append('    for (size_t i = 0; i < sizeof(g_bulk_id_ref) / sizeof(g_bulk_id_ref[0]); ++i) {')
        c.append('        if (g_bulk_id_ref[i].id == id) return orpheus_control_write_bulk(g_bulk_id_ref[i].node, g_bulk_id_ref[i].key, data, count);')
        c.append('    }')
        c.append('    return -1;')
        c.append('}')
        c.append('')
        c.append('int orpheus_control_get_bulk_id(uint32_t id, void* out, size_t count) {')
        c.append('    for (size_t i = 0; i < sizeof(g_bulk_id_ref) / sizeof(g_bulk_id_ref[0]); ++i) {')
        c.append('        if (g_bulk_id_ref[i].id == id) return orpheus_control_get_bulk(g_bulk_id_ref[i].node, g_bulk_id_ref[i].key, out, count);')
        c.append('    }')
        c.append('    return -1;')
        c.append('}')
        c.append('')
        c.append('static const struct { uint32_t id; const char* node; const char* key; } g_all_id_ref[] = {')
        if plan.id_map:
            for e in plan.id_map:
                c.append(f'    {{ 0x{e["id"]:08X}U, "{e["node"]}", "{e["key"]}" }},')
        else:
            c.append('    { 0U, "", "" },')
        c.append('};')
        c.append('')
        c.append('/* 外部注册 hook（消息路径：外部 hook 优先于默认槽语义；CUSTOM 必须由 hook 处理） */')
        c.append('#define ORPHEUS_GEN_MAX_HOOKS 16u')
        c.append('static struct { uint32_t id; OrpheusHookFn fn; void* ctx; } g_hooks[ORPHEUS_GEN_MAX_HOOKS];')
        c.append('static size_t g_hook_count = 0;')
        c.append('')
        c.append('int orpheus_control_register_hook(uint32_t id, OrpheusHookFn fn, void* ctx) {')
        c.append('    if (!fn || g_hook_count >= ORPHEUS_GEN_MAX_HOOKS) return -1;')
        c.append('    g_hooks[g_hook_count].id = id;')
        c.append('    g_hooks[g_hook_count].fn = fn;')
        c.append('    g_hooks[g_hook_count].ctx = ctx;')
        c.append('    g_hook_count++;')
        c.append('    return 0;')
        c.append('}')
        c.append('')
        c.append('static OrpheusGenSlot* orpheus_control_find_by_id(uint32_t id) {')
        c.append('    for (size_t i = 0; i < sizeof(g_all_id_ref) / sizeof(g_all_id_ref[0]); ++i) {')
        c.append('        if (g_all_id_ref[i].id == id) {')
        c.append('            return orpheus_control_find(g_all_id_ref[i].node, g_all_id_ref[i].key);')
        c.append('        }')
        c.append('    }')
        c.append('    return NULL;')
        c.append('}')
        c.append('')
        c.append('/* 二进制消息：CALL → 同步 RESPONSE（回显 call_id）；NOTIFICATION → 单向分发（无返回）。 */')
        c.append('int orpheus_control_message(const uint8_t* in, size_t in_len, uint8_t* out,')
        c.append('                            size_t out_cap, size_t* out_len) {')
        c.append('    if (!in || in_len < sizeof(OrpheusMessageHeader) || !out || !out_len) return -1;')
        c.append('    *out_len = 0;')
        c.append('    const OrpheusMessageHeader* hdr = (const OrpheusMessageHeader*)in;')
        c.append('    size_t words = ORPHEUS_MSG_PAYLOAD_WORDS(hdr);')
        c.append('    if (sizeof(OrpheusMessageHeader) + words * 4 > in_len) return -1;')
        c.append('    uint32_t route = hdr->route_id;')
        c.append('    uint32_t call_id = ORPHEUS_MSG_CALL_ID(hdr);')
        c.append('    OrpheusBlob req = { in + sizeof(OrpheusMessageHeader), (uint32_t)(words * 4) };')
        c.append('    uint32_t type = ORPHEUS_MSG_TYPE(hdr);')
        c.append('    if (type == ORPHEUS_MSG_NOTIFICATION) {')
        c.append('        for (size_t i = 0; i < g_hook_count; ++i) {')
        c.append('            if (g_hooks[i].id == route) { g_hooks[i].fn(g_hooks[i].ctx, route, ORPHEUS_EVENT_CUSTOM, &req, NULL); break; }')
        c.append('        }')
        c.append('        return 0;')
        c.append('    }')
        c.append('    if (type != ORPHEUS_MSG_CALL) return -1;')
        c.append('    OrpheusBlob resp = { out + sizeof(OrpheusMessageHeader), 0 };')
        c.append('    uint32_t resp_words = 0, resp_flags = 0;')
        c.append('    int handled = 0;')
        c.append('    for (size_t i = 0; i < g_hook_count && !handled; ++i) {')
        c.append('        if (g_hooks[i].id != route) continue;')
        c.append('        int r = g_hooks[i].fn(g_hooks[i].ctx, route, ORPHEUS_EVENT_CUSTOM, &req, &resp);')
        c.append('        if (r == ORPHEUS_HOOK_ERROR || resp.len > out_cap - sizeof(OrpheusMessageHeader)) {')
        c.append('            resp_flags = ORPHEUS_MSG_FLAG_ERROR;')
        c.append('        } else if (r == ORPHEUS_HOOK_HANDLED) {')
        c.append('            resp_words = (resp.len + 3) / 4;')
        c.append('        }')
        c.append('        handled = 1;')
        c.append('    }')
        c.append('    if (!handled) {')
        c.append('        OrpheusGenSlot* s = orpheus_control_find_by_id(route);')
        c.append('        if (!s) {')
        c.append('            resp_flags = ORPHEUS_MSG_FLAG_ERROR;  /* CUSTOM 必须由 hook 处理 */')
        c.append('        } else if (words > 0) {')
        c.append('            if (s->kind == ORPHEUS_SLOT_PROBE || s->kind == ORPHEUS_SLOT_STATE) {')
        c.append('                resp_flags = ORPHEUS_MSG_FLAG_ERROR;  /* 只读 */')
        c.append('            } else if (s->kind == ORPHEUS_SLOT_BULK) {')
        c.append('                if (orpheus_control_write_bulk(s->node, s->key, req.data, req.len / 4) != 0) resp_flags = ORPHEUS_MSG_FLAG_ERROR;')
        c.append('            } else if (req.len >= 4) {')
        c.append('                if (s->type == ORPHEUS_VALUE_FLOAT) memcpy((char*)s->state + s->offset, req.data, 4);')
        c.append('                else if (s->type == ORPHEUS_VALUE_INT) memcpy((char*)s->state + s->offset, req.data, 4);')
        c.append('                else if (s->type == ORPHEUS_VALUE_BOOL) *(uint8_t*)((char*)s->state + s->offset) = *(const uint8_t*)req.data != 0;')
        c.append('                else resp_flags = ORPHEUS_MSG_FLAG_ERROR;')
        c.append('            } else {')
        c.append('                resp_flags = ORPHEUS_MSG_FLAG_ERROR;')
        c.append('            }')
        c.append('        } else {')
        c.append('            if (s->kind == ORPHEUS_SLOT_BULK) {')
        c.append('                if (s->count * 4 > out_cap - sizeof(OrpheusMessageHeader)) { resp_flags = ORPHEUS_MSG_FLAG_ERROR; }')
        c.append('                else if (orpheus_control_get_bulk(s->node, s->key, out + sizeof(OrpheusMessageHeader), s->count) == 0) resp_words = s->count;')
        c.append('                else resp_flags = ORPHEUS_MSG_FLAG_ERROR;')
        c.append('            } else if (s->type == ORPHEUS_VALUE_FLOAT) { memcpy(out + sizeof(OrpheusMessageHeader), (char*)s->state + s->offset, 4); resp_words = 1; }')
        c.append('            else if (s->type == ORPHEUS_VALUE_INT) { memcpy(out + sizeof(OrpheusMessageHeader), (char*)s->state + s->offset, 4); resp_words = 1; }')
        c.append('            else if (s->type == ORPHEUS_VALUE_BOOL) { out[sizeof(OrpheusMessageHeader)] = *(uint8_t*)((char*)s->state + s->offset); resp_words = 1; }')
        c.append('            else resp_flags = ORPHEUS_MSG_FLAG_ERROR;')
        c.append('        }')
        c.append('    }')
        c.append('    if (out_cap < sizeof(OrpheusMessageHeader) + resp_words * 4) return -1;')
        c.append('    OrpheusMessageHeader* rh = (OrpheusMessageHeader*)out;')
        c.append('    rh->route_id = route;')
        c.append('    rh->bits = ORPHEUS_MSG_MAKE(ORPHEUS_MSG_RESPONSE, resp_flags, call_id, resp_words);')
        c.append('    *out_len = sizeof(OrpheusMessageHeader) + resp_words * 4;')
        c.append('    return 0;')
        c.append('}')
        (src_dir / "orpheus_control.c").write_text("\n".join(c) + "\n", encoding="utf-8")

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
        if (output_dir / "src" / "platform_hooks.c").exists():
            app_sources += " src/platform_hooks.c"
        if (output_dir / "src" / "orpheus_id_map.c").exists():
            app_sources += " src/orpheus_id_map.c"
        if (output_dir / "src" / "orpheus_control.c").exists():
            app_sources += " src/orpheus_control.c"
        lines.append(f'add_executable(orpheus_generated_app {app_sources})')
        libs = " ".join(self._component_target_name(cid) for cid in component_ids)
        lines.append(f'target_link_libraries(orpheus_generated_app {libs})')

        with open(output_dir / "CMakeLists.txt", "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
