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

    @staticmethod
    def _schedule_tick(plan: ExecutionPlan) -> int:
        """静态调度主步长（图速率帧）；旧 plan 无 schedule 时回退 plan.block_size。"""
        schedule = getattr(plan, "schedule", None) or {}
        return int(schedule.get("tick", 0) or plan.block_size)

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

        # win 实时宿主模式：图含设备组件（device_in/device_out）时，生成 miniaudio
        # 设备时钟宿主（模板 host_win.c 原样复制，协议与 rt_host 一致）；否则保持
        # 文件时钟/嵌入骨架。平台解析（resolve.py）已保证设备组件只在 win 下出现。
        device_in_nodes = [
            nid for nid in plan.execution_order
            if plan.node_configs[nid]["component"] == "orpheus.builtin.device_in"
        ]
        device_out_nodes = [
            nid for nid in plan.execution_order
            if plan.node_configs[nid]["component"] == "orpheus.builtin.device_out"
        ]
        win_host = bool(device_in_nodes or device_out_nodes)

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
        self._generate_main_c(plan, component_ids, src_dir / "main.c",
                              self.uart_link_decls(plan), win_host=win_host,
                              device_in_nodes=device_in_nodes,
                              device_out_nodes=device_out_nodes)
        if win_host:
            self._generate_host_config(plan, device_in_nodes, device_out_nodes,
                                       include_dir / "orpheus_host_config.h")
            # 宿主接口头：init/process + 设备 buffer 访问器 + arena 基址
            gen_h = [
                '#ifndef ORPHEUS_GENERATED_H',
                '#define ORPHEUS_GENERATED_H',
                '#include "orpheus_abi.h"',
                '/* 图本体接口（main.c 实现）：win 宿主（host_win.c）与嵌入宿主共用。 */',
                'int orpheus_generated_init(uint32_t sample_rate, uint32_t block_size);',
                'int orpheus_generated_process(uint32_t frame_count);',
                'void orpheus_generated_teardown(void);  /* 逆执行序销毁（宿主退出时调用） */',
                '/* 设备 buffer：宿主在 process 前填 device_in 输出、process 后取 device_out 输入。 */',
                'OrpheusBuffer* orpheus_host_device_in_buffer(void);   /* 未连接返回 NULL */',
                'OrpheusBuffer* orpheus_host_device_out_buffer(void);  /* 未连接返回 NULL */',
                'void* orpheus_arena_base(void);  /* 状态 arena 基址（RESOLVE 用），无状态返回 NULL */',
                '#endif /* ORPHEUS_GENERATED_H */',
            ]
            (include_dir / "orpheus_generated.h").write_text(
                "\n".join(gen_h) + "\n", encoding="utf-8")
            # 宿主模板：仓库内真实 C 文件（单一事实来源），原样复制进生成工程
            template = Path(__file__).parent / "templates" / "host_win.c"
            shutil.copy2(template, src_dir / "host_win.c")
            ma_header = self.project_root / "third_party" / "miniaudio.h"
            shutil.copy2(ma_header, include_dir / "miniaudio.h")

        # 嵌入 I/O 适配模板：存在 embed_in/embed_out 节点时生成，用户按硬件填充
        embed_nodes = [
            nid for nid in plan.execution_order
            if plan.node_configs[nid]["component"]
            in ("orpheus.builtin.embed_in", "orpheus.builtin.embed_out")
        ]
        if embed_nodes:
            self._generate_platform_io(plan, src_dir / "platform_io.c")

        # 声明式平台节点（execution.none）：按 codegen_template 分发代码生成模板
        if plan.declarations:
            self._generate_declarations(plan, src_dir, include_dir)

        # 数据 ID（32 位宏）+ ID map + 内存布局（模块嵌套 arena 定义在 include/orpheus_arena.h）
        self._generate_ids(plan, include_dir, src_dir, output_dir)

        # 生成路径控制 API + BULK 双 bank（可选：仅工程 double_bank 生效的槽产出影子）
        self._generate_control(plan, include_dir, src_dir)

        # Generate CMakeLists.txt
        self._generate_cmake(plan, all_ids, output_dir)

    def _generate_host_config(
        self,
        plan: ExecutionPlan,
        device_in_nodes: list[str],
        device_out_nodes: list[str],
        path: Path,
    ) -> None:
        """生成 win 宿主的设备配置宏头（orpheus_host_config.h）。

        host_win.c 模板不感知图细节，全部设备参数经此头注入；
        多 device_in/device_out 时取第一个（与 rt_host 一致）。
        """
        def params_of(nodes: list[str]) -> dict[str, Any]:
            if not nodes:
                return {}
            return plan.node_configs[nodes[0]].get("params", {})

        in_params = params_of(device_in_nodes)
        out_params = params_of(device_out_nodes)

        def as_int(v: Any, default: int) -> int:
            try:
                return int(float(v))
            except (TypeError, ValueError):
                return default

        in_ch = as_int(in_params.get("channels", 2), 2)
        out_ch = as_int(out_params.get("channels", 2), 2)
        in_device = str(in_params.get("device", "") or "")
        out_device = str(out_params.get("device", "") or "")
        loopback = str(in_params.get("source", "microphone") or "microphone") == "loopback"

        lines = [
            '#ifndef ORPHEUS_HOST_CONFIG_H',
            '#define ORPHEUS_HOST_CONFIG_H',
            '/* orpheus_host_config.h —— win 实时宿主设备配置（自动生成，勿手改）。',
            ' * host_win.c 模板的全部图相关参数由此注入。 */',
            f'#define ORPHEUS_HOST_HAS_IN {1 if device_in_nodes else 0}',
            f'#define ORPHEUS_HOST_HAS_OUT {1 if device_out_nodes else 0}',
            f'#define ORPHEUS_HOST_LOOPBACK {1 if loopback else 0}',
            f'#define ORPHEUS_HOST_IN_DEVICE "{self._c_escape(in_device)}"  /* 设备名称子串（大小写不敏感），空=默认 */',
            f'#define ORPHEUS_HOST_OUT_DEVICE "{self._c_escape(out_device)}"',
            f'#define ORPHEUS_HOST_IN_CHANNELS {in_ch}u',
            f'#define ORPHEUS_HOST_OUT_CHANNELS {out_ch}u',
            f'#define ORPHEUS_HOST_SAMPLE_RATE {int(plan.sample_rate)}u',
            f'#define ORPHEUS_HOST_BLOCK_SIZE {self._schedule_tick(plan)}u  /* 静态调度主步长（图速率帧） */',
            f'#define ORPHEUS_HOST_BUFFER_FRAMES {int(plan.buffer_size)}u  /* 异步桥环形缓冲容量（帧），0=自动 采样率/10 */',
            '#endif /* ORPHEUS_HOST_CONFIG_H */',
        ]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def _generate_main_c(self, plan: ExecutionPlan, component_ids: list[str], path: Path,
                         link_nodes: list[dict[str, Any]] | None = None,
                         win_host: bool = False,
                         device_in_nodes: list[str] | None = None,
                         device_out_nodes: list[str] | None = None) -> None:
        link_nodes = link_nodes or []
        device_in_nodes = device_in_nodes or []
        device_out_nodes = device_out_nodes or []
        lines: list[str] = []
        lines.append('#include <stdio.h>')
        lines.append('#include <stdlib.h>')
        lines.append('#include <string.h>')
        lines.append('#include "orpheus_abi.h"')
        if win_host:
            lines.append('#include "orpheus_generated.h"')
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
        if link_nodes:
            for d in link_nodes:
                lines.append(f'#include "orpheus_link_{self._uart_link_sym(d)}.h"')
            lines.append('#include <threads.h>')
            lines.append('#include <time.h>')
            lines.append('#ifdef _WIN32')
            lines.append('#include <io.h>')
            lines.append('#include <fcntl.h>')
            lines.append('#endif')
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

        # rate-bridge 分析：合流（merge）节点输入边。源端口直写 staging（自己的块长），
        # 每次触发后按写游标 memcpy 滚入桥接 buffer（深度=合流量子），merge 节点在
        # 同步点整块读出——与动态路径 Runtime 的 BridgeCopy 同语义。
        bridge_srcs: dict[str, dict[str, Any]] = {}  # "node:port" -> staging 信息
        bridge_copies: list[dict[str, Any]] = []     # 每条桥接边一次滚动拷贝
        for conn in plan.connections:
            if not plan.buffers[conn["buffer"]].get("rate_bridge"):
                continue
            src = conn["from"]
            if src not in bridge_srcs:
                from_node, from_port = src.split(":", 1)
                from_cfg = plan.node_configs[from_node]
                stride = int(from_cfg.get("output_port_block_sizes", {}).get(from_port, 0)) \
                    or int(from_cfg.get("frames", 0))
                channels = int(from_cfg.get("output_port_channels", {}).get(from_port, 1))
                bridge_srcs[src] = {
                    "sym": f'{self._sanitized_node_id(from_node)}_{self._sanitized_node_id(from_port)}',
                    "stride": stride,
                    "channels": channels,
                }
            bridge_copies.append({
                "src": src,
                "to": conn["to"],
                "src_node": src.split(":", 1)[0],
                "sym": bridge_srcs[src]["sym"],
                "buf": conn["buffer"].replace("-", "_").replace(".", "_"),
                "stride": bridge_srcs[src]["stride"],
                "channels": bridge_srcs[src]["channels"],
                "depth": int(plan.buffers[conn["buffer"]]["frame_count"]),
            })

        # Buffer declarations
        for buf_id, buf in plan.buffers.items():
            s_buf = buf_id.replace("-", "_").replace(".", "_")
            samples = buf["frame_count"] * buf["channels"]
            lines.append(f'static float g_buf_{s_buf}[{samples}];')
            lines.append(f'static OrpheusBuffer g_buffer_{s_buf} = {{0}};')
        # rate-bridge staging 缓冲与写游标
        for src, info in bridge_srcs.items():
            lines.append(f'static float g_stage_buf_{info["sym"]}[{info["stride"] * info["channels"]}];')
            lines.append(f'static OrpheusBuffer g_stage_{info["sym"]} = {{0}};')
        for i in range(len(bridge_copies)):
            lines.append(f'static uint32_t g_bridge_cursor_{i} = 0;')
        lines.append("")

        # Input/output buffer pointer arrays per node, sized by the ordered port
        # lists and bound by port id (unconnected pins stay NULL).
        # rate-bridge：桥接源端口一律绑定 staging；普通消费方共享 staging，
        # 桥接消费方（merge 输入）绑定桥接 buffer。
        port_buffer: dict[str, str] = {}  # "node:port" -> buffer global name
        fanout_buf: dict[str, str] = {}   # source "node:port" -> first buffer id
        for conn in plan.connections:
            s_buf = conn["buffer"].replace("-", "_").replace(".", "_")
            if conn["from"] in bridge_srcs:
                staging = f'&g_stage_{bridge_srcs[conn["from"]]["sym"]}'
                port_buffer[conn["from"]] = staging
                if plan.buffers[conn["buffer"]].get("rate_bridge"):
                    port_buffer[conn["to"]] = f'&g_buffer_{s_buf}'
                else:
                    port_buffer[conn["to"]] = staging
                continue
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

        # ---- 控制链路（两相快照 control_tick；语义与动态路径 Runtime::control_tick 一致：
        #      每图块一次，先读全部源到快照、再写全部目标，经组件 ABI get/set_parameter） ----
        ctl_links = list(getattr(plan, "control_links", []) or [])

        def _fmt_shape(shape: list) -> str:
            return "标量" if not shape else "×".join(str(int(d)) for d in shape)

        if ctl_links:
            lines.append('/* ==== 控制链路（块边界两相快照：先全读、后全写，每链 1 块延迟，')
            lines.append('     顺序无关、闭环合法；与动态路径 Runtime::control_tick 同语义） ==== */')
            lines.append(f'static OrpheusValue g_ctl_snap[{len(ctl_links)}];  /* 控制帧快照 */')
            lines.append(f'static int g_ctl_ok[{len(ctl_links)}];             /* 源读取成功标志 */')
            for i, link in enumerate(ctl_links):
                if link["type"] == "string":
                    lines.append(
                        f'static char g_ctl_str{i}[256];  /* string 链快照缓冲（256B 上限，超长截断） */'
                    )
            lines.append('')
            lines.append('static void control_tick(void) {')
            lines.append('    /* 第一相：读 —— 全部源参数 → 快照（经组件 get_parameter） */')
            for i, link in enumerate(ctl_links):
                desc = (f'{link["src_node"]}.{link["src_param"]} [{_fmt_shape(link["shape"])}]'
                        f' -> {link["dst_node"]}.{link["dst_param"]}')
                if link["type"] != "string" and int(link.get("count", 1)) > 1:
                    lines.append(
                        f'    /* {desc}：数组链（count={int(link["count"])}）本期不执行，仅编译期形状校验 */'
                    )
                    continue
                s_src = self._sanitized_node_id(link["src_node"])
                lines.append(f'    /* {desc} */')
                lines.append(
                    f'    g_ctl_ok[{i}] = (g_iface_{s_src}->get_parameter != NULL &&'
                )
                lines.append(
                    f'        g_iface_{s_src}->get_parameter(g_state_{s_src}, "{link["src_param"]}",'
                    f' &g_ctl_snap[{i}]) == ORPHEUS_OK);'
                )
                if link["type"] == "string":
                    lines.append(f'    if (g_ctl_ok[{i}]) {{  /* 字符串快照拷入静态缓冲（截断 255B + NUL） */')
                    lines.append(f'        const char* p_ = g_ctl_snap[{i}].value.str != NULL')
                    lines.append(f'                         ? g_ctl_snap[{i}].value.str : "";')
                    lines.append('        size_t n_ = strlen(p_);')
                    lines.append(f'        if (n_ >= sizeof(g_ctl_str{i})) n_ = sizeof(g_ctl_str{i}) - 1;')
                    lines.append(f'        memcpy(g_ctl_str{i}, p_, n_);')
                    lines.append(f'        g_ctl_str{i}[n_] = \'\\0\';')
                    lines.append('    }')
            lines.append('')
            lines.append('    /* 第二相：写 —— 快照 → 全部目标（经组件 set_parameter） */')
            for i, link in enumerate(ctl_links):
                if link["type"] != "string" and int(link.get("count", 1)) > 1:
                    continue  # 数组链：第一相已注记，本相同样跳过
                s_dst = self._sanitized_node_id(link["dst_node"])
                lines.append(f'    if (g_ctl_ok[{i}] && g_iface_{s_dst}->set_parameter != NULL) {{')
                if link["type"] == "string":
                    lines.append(f'        g_ctl_snap[{i}].value.str = g_ctl_str{i};')
                lines.append(
                    f'        (void)g_iface_{s_dst}->set_parameter(g_state_{s_dst}, "{link["dst_param"]}",'
                    f' &g_ctl_snap[{i}]);'
                )
                lines.append('    }')
            lines.append('}')
            lines.append("")

        # Init function（win 宿主模式下非 static，供 host_win.c 调用）
        init_qual = '' if win_host else 'static '
        lines.append(f'{init_qual}int orpheus_generated_init(uint32_t sample_rate, uint32_t block_size) {{')
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
        # rate-bridge staging buffer 初始化
        for src, info in bridge_srcs.items():
            lines.append(f'    g_stage_{info["sym"]}.data = g_stage_buf_{info["sym"]};')
            lines.append(f'    g_stage_{info["sym"]}.format = ORPHEUS_FORMAT_F32;')
            lines.append(f'    g_stage_{info["sym"]}.channels = {info["channels"]};')
            lines.append(f'    g_stage_{info["sym"]}.frame_capacity = {info["stride"]};')
            lines.append(f'    g_stage_{info["sym"]}.frame_count = {info["stride"]};')
            lines.append(f'    g_stage_{info["sym"]}.interleaved = 1;')
        lines.append("")

        # Create and prepare instances (with per-node parameter tables)
        for node_id in plan.execution_order:
            cfg = plan.node_configs[node_id]
            s = self._sanitized_node_id(node_id)
            params = cfg.get("params", {})
            channels = int(float(params.get("channels", 2)))
            lines.append(f'    config.channels = {channels};')
            node_sr = cfg.get("sample_rate", 0) or plan.sample_rate
            node_bs = cfg.get("block_size", 0) or cfg.get("frames", 0) or plan.block_size
            lines.append(f'    config.sample_rate = {node_sr};')
            lines.append(f'    config.block_size = {node_bs};')
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
        for d in link_nodes:
            lines.append(f'    orpheus_link_{self._uart_link_sym(d)}_init();')
        lines.append('    return ORPHEUS_OK;')
        lines.append('}')
        lines.append("")

        # Process function (multi-rate: per-node frames + schedule-period-gated firing)
        lines.append('static uint64_t g_block_counter = 0;')
        proc_qual = '' if win_host else 'static '
        lines.append(f'{proc_qual}int orpheus_generated_process(uint32_t frame_count) {{')
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
            # 静态调度：优先编译器推导的 period（主 tick 数），旧 plan 回退 divisor
            period = int(cfg.get("period", 0) or cfg.get("divisor", 1))
            frames = cfg.get("frames", 0)
            if period > 1:
                lines.append(f'    if ((g_block_counter + 1) % {period} == 0) {{')
            indent = '        ' if period > 1 else '    '
            lines.append(f'{indent}ctx.state = g_state_{s};')
            node_sr = cfg.get("sample_rate", 0) or plan.sample_rate
            lines.append(f'{indent}ctx.sample_rate = {node_sr};')
            lines.append(f'{indent}ctx.frame_count = {frames} > 0 ? {frames} : frame_count;')
            lines.append(f'{indent}ctx.inputs = (const OrpheusBuffer* const*){in_name};')
            lines.append(f'{indent}ctx.outputs = {out_name};')
            lines.append(f'{indent}ctx.input_count = {n_in};')
            lines.append(f'{indent}ctx.output_count = {n_out};')
            lines.append(f'{indent}rc = g_iface_{s}->process(g_state_{s}, &ctx);')
            lines.append(f'{indent}if (rc != ORPHEUS_OK) return rc;')
            # rate-bridge：本节点是桥接源时，触发后把 staging 的新鲜块滚入桥接 buffer
            for i, cp in enumerate(bridge_copies):
                if cp["src_node"] != node_id:
                    continue
                lines.append(f'{indent}/* rate-bridge: {cp["src"]} -> {cp["to"]} */')
                lines.append(f'{indent}memcpy(g_buf_{cp["buf"]} + g_bridge_cursor_{i} * {cp["channels"]},')
                lines.append(f'{indent}       g_stage_buf_{cp["sym"]}, {cp["stride"] * cp["channels"]} * sizeof(float));')
                lines.append(f'{indent}g_bridge_cursor_{i} = (g_bridge_cursor_{i} + {cp["stride"]}) % {cp["depth"]};')
            if period > 1:
                lines.append('    }')
        if embed_in_nodes or embed_out_nodes:
            lines.append('')
            lines.append('    orpheus_platform_io_post_block();')
        if ctl_links:
            lines.append('')
            lines.append('    control_tick();  /* 控制链路：两相快照（先全读后全写），每图块一次 */')
        lines.append('    g_block_counter++;')
        lines.append('    return ORPHEUS_OK;')
        lines.append('}')
        lines.append("")

        if win_host:
            # win 宿主（host_win.c）接口：设备 buffer 访问器 + arena 基址（RESOLVE 用）。
            # device_in 的输出 buffer 由宿主在 process 前填充；device_out 的输入 buffer
            # 由宿主在 process 后取走（与 rt_host 的职责划分一致）。
            def _port_buf(node_id: str, port_id: str) -> str | None:
                return port_buffer.get(f"{node_id}:{port_id}")

            in_buf = _port_buf(device_in_nodes[0], "out") if device_in_nodes else None
            out_buf = _port_buf(device_out_nodes[0], "in") if device_out_nodes else None
            lines.append('/* win 宿主接口（orpheus_generated.h） */')
            lines.append('OrpheusBuffer* orpheus_host_device_in_buffer(void) {')
            lines.append(f'    return {in_buf if in_buf else "NULL"};')
            lines.append('}')
            lines.append('OrpheusBuffer* orpheus_host_device_out_buffer(void) {')
            lines.append(f'    return {out_buf if out_buf else "NULL"};')
            lines.append('}')
            if any(self._state_type(nid, plan) for nid in plan.execution_order):
                lines.append('void* orpheus_arena_base(void) { return &g_arena; }')
            else:
                lines.append('void* orpheus_arena_base(void) { return NULL; }')
            # 逆执行序销毁（宿主退出时调用，让 wav_out 等落盘冲刷）
            lines.append('void orpheus_generated_teardown(void) {')
            for node_id in reversed(plan.execution_order):
                s = self._sanitized_node_id(node_id)
                lines.append(f'    g_iface_{s}->destroy(g_state_{s});')
            lines.append('}')
            lines.append("")

        if link_nodes:
            first = self._uart_link_sym(link_nodes[0])
            lines.append('/* --link-stdio：stdin/stdout 即链路（PC 冒烟）。读线程把 stdin 字节喂给链路层；')
            lines.append('   多个 uart_link 节点时 stdio 冒烟只接第一个（嵌入式由用户把各实例接各自 UART）。 */')
            lines.append('static int orpheus_link_stdio_reader(void* arg) {')
            lines.append('    (void)arg;')
            lines.append('    for (;;) {')
            lines.append('        /* 逐字节读：fread(512) 会等满缓冲才返回，getchar 来一个字节喂一个字节 */')
            lines.append('        int c = getchar();')
            lines.append('        if (c == EOF) break;')
            lines.append('        uint8_t b = (uint8_t)c;')
            lines.append(f'        orpheus_link_{first}_feed(&b, 1);')
            lines.append('    }')
            lines.append('    return 0;')
            lines.append('}')
            lines.append("")

        if not win_host:
            # 文件时钟缺省宿主 main()；win 宿主模式的 main() 在 host_win.c（设备时钟）
            self._emit_file_clock_main(lines, plan, link_nodes)

        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))

    def _emit_file_clock_main(self, lines: list[str], plan: ExecutionPlan,
                              link_nodes: list[dict[str, Any]]) -> None:
        """文件时钟缺省宿主 main()：块循环 + 控制 CLI（--write-bulk/--msg 等）。"""
        # 静态调度主步长：宿主按 tick 推进（旧 plan 回退 plan.block_size）
        tick = self._schedule_tick(plan)
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
        lines.append("    int start_i = (argc > 1 && argv[1][0] != '-') ? 2 : 1;")
        lines.append('    int blocks = start_i == 2 ? atoi(argv[1]) : 1000;')
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
        if link_nodes:
            lines.append('    int link_stdio = 0;')
        lines.append('    for (int i = start_i; i < argc; ++i) {')
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
        if link_nodes:
            lines.append('        } else if (strcmp(argv[i], "--link-stdio") == 0) {')
            lines.append('            link_stdio = 1;')
        lines.append('        } else if (strcmp(argv[i], "--echo-hook") == 0 && i + 1 < argc) {')
        lines.append('            uint32_t id = (uint32_t)strtoul(argv[++i], NULL, 0);')
        lines.append('            if (orpheus_control_register_hook(id, orpheus_echo_hook, NULL) != 0) {')
        lines.append('                printf("ERR ECHO-HOOK 0x%08X\\n", id);')
        lines.append('                return 1;')
        lines.append('            }')
        lines.append('            control_mode = 1;')
        lines.append('        }')
        lines.append('    }')
        if link_nodes:
            lines.append('    if (link_stdio) {')
            lines.append('#ifdef _WIN32')
            lines.append('        _setmode(_fileno(stdin), _O_BINARY);')
            lines.append('        _setmode(_fileno(stdout), _O_BINARY);')
            lines.append('#endif')
            lines.append('        thrd_t link_thread;')
            lines.append('        thrd_create(&link_thread, orpheus_link_stdio_reader, NULL);')
            lines.append('        struct timespec link_ts;')
            lines.append('        for (;;) {')
            lines.append(f'            if (orpheus_generated_process({tick}) != ORPHEUS_OK) return 1;')
            lines.append('            /* 冒烟 harness 用真实时间驱动探针泵（全速跑块时不会洪泛链路）；')
            lines.append('               嵌入式用户把 poll 换成自己的系统 tick（如 HAL_GetTick()）。 */')
            lines.append('            timespec_get(&link_ts, TIME_UTC);')
            lines.append('            uint32_t now_ms = (uint32_t)(link_ts.tv_sec * 1000u + link_ts.tv_nsec / 1000000u);')
            for d in link_nodes:
                lines.append(f'            orpheus_link_{self._uart_link_sym(d)}_poll(now_ms);')
            lines.append('        }')
            lines.append('    }')
        lines.append('    if (control_mode) {')
        lines.append('        for (int i = 0; i < run_blocks; ++i) {')
        lines.append(f'            if (orpheus_generated_process({tick}) != ORPHEUS_OK) return 1;')
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
        lines.append(f'        rc = orpheus_generated_process({tick});')
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

    # 组件 id → 缺省代码生成模板（manifest codegen_template 字段优先于此回退）
    _DECL_TEMPLATE_BY_COMPONENT = {
        "orpheus.builtin.platform_hook": "platform_hooks",
        "orpheus.builtin.uart_link": "uart_link",
    }

    def _decl_template(self, decl: dict[str, Any]) -> str | None:
        """declaration 节点的代码生成模板：manifest codegen_template 优先，组件 id 回退。"""
        info = self.registry.get(decl["component"])
        tmpl = (info.manifest.get("codegen_template") if info else None)
        if tmpl:
            return str(tmpl)
        return self._DECL_TEMPLATE_BY_COMPONENT.get(decl["component"])

    def _generate_declarations(self, plan: ExecutionPlan, src_dir: Path, include_dir: Path) -> None:
        """声明式平台节点（execution.none）代码生成：按模板分发。"""
        templates = {self._decl_template(d) for d in plan.declarations}
        if "platform_hooks" in templates:
            self._generate_platform_hooks(
                plan, src_dir / "platform_hooks.c", include_dir / "orpheus_platform_hooks.h"
            )
        if "uart_link" in templates:
            self._generate_uart_link(plan, src_dir, include_dir)

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

    def uart_link_decls(self, plan: ExecutionPlan) -> list[dict[str, Any]]:
        """plan.declarations 中模板为 uart_link 的节点（main.c/CMake 也用）。"""
        return [d for d in plan.declarations if self._decl_template(d) == "uart_link"]

    def _uart_link_sym(self, decl: dict[str, Any]) -> str:
        name = str(decl["params"].get("link_name") or decl["id"])
        return self._sanitized_node_id(name)

    def _generate_uart_link(self, plan: ExecutionPlan, src_dir: Path, include_dir: Path) -> None:
        """uart_link 节点 → OLINK 串口链路段（feed/poll 生成物 + init/send USER CODE 骨架）。"""
        nodes = self.uart_link_decls(plan)
        if not nodes:
            return
        # 自包含：OLINK 成帧层随工程复制
        abi_src = self.project_root / "orpheus_abi"
        shutil.copy2(abi_src / "include" / "orpheus_olink.h", include_dir / "orpheus_olink.h")
        shutil.copy2(abi_src / "src" / "olink.c", src_dir / "olink.c")
        probes = [e for e in plan.id_map if e["kind"] == "PROBE"]
        value_type = {"float": "ORPHEUS_VALUE_FLOAT", "int": "ORPHEUS_VALUE_INT",
                      "bool": "ORPHEUS_VALUE_BOOL", "string": "ORPHEUS_VALUE_STRING"}
        for d in nodes:
            s = self._uart_link_sym(d)
            params = d["params"]
            baud = int(float(params.get("baud", 921600) or 921600))
            interval = float(params.get("probe_interval_ms", 200.0) or 0.0)
            note = str(params.get("note") or "")
            hdr = [
                f'#ifndef ORPHEUS_LINK_{s.upper()}_H',
                f'#define ORPHEUS_LINK_{s.upper()}_H',
                '#include <stdint.h>',
                '#ifdef __cplusplus',
                'extern "C" {',
                '#endif',
                '',
                f'/* uart_link 节点 {d["id"]} · 声明波特率 {baud}（实际由 init 的实现决定）'
                + (f' · {note}' if note else '') + ' */',
                f'#define ORPHEUS_LINK_{s.upper()}_PROBE_INTERVAL_MS {interval:.1f}f',
                '',
                f'void orpheus_link_{s}_init(void);                      /* USER CODE：串口/DMA 初始化 */',
                f'int32_t orpheus_link_{s}_send(const uint8_t* data, uint32_t len);  /* USER CODE：发送实现 */',
                f'void orpheus_link_{s}_feed(const uint8_t* data, uint32_t len);     /* 你的 onRecv/UART 回调里调 */',
                f'void orpheus_link_{s}_poll(uint32_t now_ms);           /* 主循环周期调用（探针泵） */',
                '',
                '#ifdef __cplusplus',
                '}',
                '#endif',
                f'#endif /* ORPHEUS_LINK_{s.upper()}_H */',
                '',
            ]
            (include_dir / f"orpheus_link_{s}.h").write_text("\n".join(hdr), encoding="utf-8")

            c = [
                f'/* orpheus_link_{s}.c —— OLINK 串口链路段（自动生成；重新生成会覆盖）。',
                ' * 字节流 → COBS+CRC16 解码 → orpheus_control_message 分发 → RESPONSE 经用户 send 回发；',
                ' * 探针泵：内部构造读 CALL 本地分发，结果包 NOTIFICATION 上行。 */',
                f'#include "orpheus_link_{s}.h"',
                '#include "orpheus_olink.h"',
                '#include "orpheus_control.h"',
                '#include <string.h>',
                '',
                'static OLinkDecoder g_dec;',
                'static uint8_t g_dec_inited;',
                'static uint8_t g_frame[OLINK_MSG_MAX];',
                'static uint8_t g_resp[OLINK_MSG_MAX];',
                'static uint32_t g_last_probe_ms;',
                '',
                '/* 探针表（kind=PROBE 的数据点） */',
                'typedef struct { uint32_t id; uint32_t type; uint32_t count; } OrpheusLinkProbe;',
                'static const OrpheusLinkProbe g_probes[] = {',
            ]
            for e in probes:
                c.append(f'    {{ 0x{e["id"]:08X}U, {value_type.get(e["type"], "ORPHEUS_VALUE_FLOAT")}, {e["count"]}u }},  /* {e["node"]}.{e["key"]} */')
            if not probes:
                c.append('    { 0U, 0U, 0U },')
            c += [
                '};',
                '',
                f'void orpheus_link_{s}_feed(const uint8_t* data, uint32_t len) {{',
                '    if (!g_dec_inited) { olink_decoder_init(&g_dec); g_dec_inited = 1; g_last_probe_ms = 0; }',
                '    for (uint32_t i = 0; i < len; ++i) {',
                '        uint16_t n = olink_decode_byte(&g_dec, data[i], g_frame, sizeof(g_frame));',
                '        if (n == 0) continue;',
                '        size_t out_len = 0;',
                '        if (orpheus_control_message(g_frame, n, g_resp, sizeof(g_resp), &out_len) == 0 && out_len > 0) {',
                '            uint8_t wire[OLINK_FRAME_MAX];',
                '            uint16_t m = olink_encode(g_resp, (uint16_t)out_len, wire, sizeof(wire));',
                f'            if (m > 0) orpheus_link_{s}_send(wire, m);',
                '        }',
                '    }',
                '}',
                '',
                '/* 对单个探针：本地构造读 CALL → 包 NOTIFICATION 帧上行 */',
                'static void link_emit_probe(const OrpheusLinkProbe* p, uint8_t* wire, uint16_t wire_cap) {',
                '    OrpheusMessageHeader call;',
                '    call.route_id = p->id;',
                '    call.bits = ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, 0, 0, 0);',
                '    size_t out_len = 0;',
                '    if (orpheus_control_message((const uint8_t*)&call, sizeof(call), g_resp, sizeof(g_resp), &out_len) != 0) return;',
                '    if (out_len < sizeof(OrpheusMessageHeader)) return;',
                '    const OrpheusMessageHeader* rh = (const OrpheusMessageHeader*)g_resp;',
                '    if (ORPHEUS_MSG_TYPE(rh) != ORPHEUS_MSG_RESPONSE) return;',
                '    uint32_t words = ORPHEUS_MSG_PAYLOAD_WORDS(rh);',
                '    /* 帧 = 通知头 || payload，整体一次 OLINK 编码后上行 */',
                '    OrpheusMessageHeader note;',
                '    note.route_id = p->id;',
                '    note.bits = ORPHEUS_MSG_MAKE(ORPHEUS_MSG_NOTIFICATION, 0, 0, words);',
                '    uint8_t frame[OLINK_MSG_MAX];',
                '    memcpy(frame, &note, sizeof(note));',
                '    memcpy(frame + sizeof(note), g_resp + sizeof(OrpheusMessageHeader), words * 4);',
                '    uint16_t m = olink_encode(frame, (uint16_t)(sizeof(note) + words * 4), wire, wire_cap);',
                f'    if (m > 0) orpheus_link_{s}_send(wire, m);',
                '}',
                '',
                f'void orpheus_link_{s}_poll(uint32_t now_ms) {{',
                f'    if (ORPHEUS_LINK_{s.upper()}_PROBE_INTERVAL_MS <= 0.0f) return;',
                '    if (!g_dec_inited) { olink_decoder_init(&g_dec); g_dec_inited = 1; g_last_probe_ms = now_ms; }',
                f'    if ((float)(now_ms - g_last_probe_ms) < ORPHEUS_LINK_{s.upper()}_PROBE_INTERVAL_MS) return;',
                '    g_last_probe_ms = now_ms;',
                '    uint8_t wire[OLINK_FRAME_MAX];',
                '    for (uint32_t i = 0; i < sizeof(g_probes) / sizeof(g_probes[0]); ++i) {',
                '        if (g_probes[i].id != 0U) link_emit_probe(&g_probes[i], wire, sizeof(wire));',
                '    }',
                '}',
                '',
            ]
            (src_dir / f"orpheus_link_{s}.c").write_text("\n".join(c), encoding="utf-8")

            hooks = [
                f'/* orpheus_link_hooks_{s}.c —— 串口链路 USER CODE（自动生成；重新生成会覆盖，请另存副本）。',
                ' * 你要实现的只有两处：init（串口/DMA 初始化）与 send（把字节发出去）。',
                ' * 收到字节在你自己的接收路径调 orpheus_link_feed，主循环调 orpheus_link_poll。 */',
                f'#include "orpheus_link_{s}.h"',
                '#ifdef ORPHEUS_LINK_STDIO',
                '#include <stdio.h>',
                '#endif',
                '',
                f'void orpheus_link_{s}_init(void) {{',
                '    /* USER CODE BEGIN init */',
                '    /* 例：USART1 初始化 + RX 中断/DMA 使能（声明波特率见头文件注释） */',
                '    /* USER CODE END init */',
                '}',
                '',
                f'int32_t orpheus_link_{s}_send(const uint8_t* data, uint32_t len) {{',
                '    /* USER CODE BEGIN send */',
                '#ifdef ORPHEUS_LINK_STDIO',
                '    /* PC 冒烟：stdout 即链路（二进制模式由 main 设置） */',
                '    int32_t n = (int32_t)fwrite(data, 1, len, stdout);',
                '    fflush(stdout);',
                '    return n;',
                '#else',
                '    (void)data; (void)len;',
                '    /* TODO: 在此实现平台 UART 发送（阻塞写/DMA 入队均可），返回已发字节数 */',
                '    return 0;',
                '#endif',
                '    /* USER CODE END send */',
                '}',
                '',
            ]
            (src_dir / f"orpheus_link_hooks_{s}.c").write_text("\n".join(hooks), encoding="utf-8")

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
            '    const char* node;       /* 叶子节点 id（模块包为 ""） */',
            '    const char* key;        /* 槽 key（模块包为 ""） */',
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
                f'offsetof(OrpheusArena, {chain}), "", "" }},'
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
                    f'{mod["id"]}, {entry["id"] & 0xFFFF}, {mod_off}, {arena_off}, '
                    f'"{nid}", "{entry["key"]}" }},'
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
            '/* 标量槽读写与探针枚举（宿主控制面用，如 win 宿主的 SET/GET/PROBE 协议）。',
            ' * 直写 state+offset，不经组件 set_parameter：调音渐变由组件 process 自行平滑；',
            ' * PROBE/STATE 只读，set 返回 -1。字符串槽按 char 数组处理（容量=size×count）。 */',
            'int orpheus_control_set_value(const char* node, const char* key, OrpheusValue v);',
            'int orpheus_control_get_value(const char* node, const char* key, OrpheusValue* out);',
            'int orpheus_control_set_value_id(uint32_t id, OrpheusValue v);   /* RW <id>：按 ID 写标量 */',
            'int orpheus_control_get_value_id(uint32_t id, OrpheusValue* out); /* RR <id>：按 ID 读标量 */',
            'size_t orpheus_control_probe_count(void);',
            'int orpheus_control_probe_get(size_t index, const char** node, const char** key, OrpheusValue* out);',
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
        c.append('/* 标量槽读写：直写 state+offset（部署形态；调音渐变由组件 process 自行平滑）。 */')
        c.append('static void orpheus_control_read_slot(const OrpheusGenSlot* s, OrpheusValue* out) {')
        c.append('    const char* addr = (const char*)s->state + s->offset;')
        c.append('    out->type = (OrpheusValueType)s->type;')
        c.append('    switch (s->type) {')
        c.append('        case ORPHEUS_VALUE_FLOAT:  out->value.f32 = *(const float*)addr; break;')
        c.append('        case ORPHEUS_VALUE_INT:    out->value.i32 = *(const int32_t*)addr; break;')
        c.append('        case ORPHEUS_VALUE_BOOL:   out->value.b = *(const bool*)addr; break;')
        c.append('        case ORPHEUS_VALUE_STRING: out->value.str = (const char*)addr; break;  /* 槽存储为 char 数组 */')
        c.append('        default: out->value.i32 = 0; break;')
        c.append('    }')
        c.append('}')
        c.append('')
        c.append('static int orpheus_control_write_slot(OrpheusGenSlot* s, OrpheusValue v) {')
        c.append('    char* addr = (char*)s->state + s->offset;')
        c.append('    /* 文本协议数值不区分 float/int，允许互转；bool 由 int/float 归一 */')
        c.append('    if (s->type == ORPHEUS_VALUE_FLOAT) {')
        c.append('        if (v.type == ORPHEUS_VALUE_FLOAT) { *(float*)addr = v.value.f32; return 0; }')
        c.append('        if (v.type == ORPHEUS_VALUE_INT)   { *(float*)addr = (float)v.value.i32; return 0; }')
        c.append('    }')
        c.append('    if (s->type == ORPHEUS_VALUE_INT) {')
        c.append('        if (v.type == ORPHEUS_VALUE_INT)   { *(int32_t*)addr = v.value.i32; return 0; }')
        c.append('        if (v.type == ORPHEUS_VALUE_FLOAT) { *(int32_t*)addr = (int32_t)v.value.f32; return 0; }')
        c.append('    }')
        c.append('    if (s->type == ORPHEUS_VALUE_BOOL) {')
        c.append('        if (v.type == ORPHEUS_VALUE_BOOL)  { *(bool*)addr = v.value.b; return 0; }')
        c.append('        if (v.type == ORPHEUS_VALUE_INT)   { *(bool*)addr = v.value.i32 != 0; return 0; }')
        c.append('        if (v.type == ORPHEUS_VALUE_FLOAT) { *(bool*)addr = v.value.f32 != 0.0f; return 0; }')
        c.append('    }')
        c.append('    if (s->type == ORPHEUS_VALUE_STRING && v.type == ORPHEUS_VALUE_STRING && v.value.str) {')
        c.append('        size_t cap = s->size * s->count;')
        c.append('        if (cap == 0) return -1;')
        c.append('        strncpy(addr, v.value.str, cap - 1);')
        c.append('        addr[cap - 1] = (char)0;')
        c.append('        return 0;')
        c.append('    }')
        c.append('    return -1;')
        c.append('}')
        c.append('')
        c.append('int orpheus_control_set_value(const char* node, const char* key, OrpheusValue v) {')
        c.append('    OrpheusGenSlot* s = orpheus_control_find(node, key);')
        c.append('    if (!s || s->kind == ORPHEUS_SLOT_PROBE || s->kind == ORPHEUS_SLOT_STATE) return -1;')
        c.append('    return orpheus_control_write_slot(s, v);')
        c.append('}')
        c.append('')
        c.append('int orpheus_control_get_value(const char* node, const char* key, OrpheusValue* out) {')
        c.append('    if (!out) return -1;')
        c.append('    OrpheusGenSlot* s = orpheus_control_find(node, key);')
        c.append('    if (!s) return -1;')
        c.append('    orpheus_control_read_slot(s, out);')
        c.append('    return 0;')
        c.append('}')
        c.append('')
        c.append('size_t orpheus_control_probe_count(void) {')
        c.append('    size_t n = 0;')
        c.append('    for (size_t i = 0; i < g_gen_slot_count; ++i)')
        c.append('        if (g_gen_slots[i].state && g_gen_slots[i].kind == ORPHEUS_SLOT_PROBE) n++;')
        c.append('    return n;')
        c.append('}')
        c.append('')
        c.append('int orpheus_control_probe_get(size_t index, const char** node, const char** key, OrpheusValue* out) {')
        c.append('    size_t seen = 0;')
        c.append('    for (size_t i = 0; i < g_gen_slot_count; ++i) {')
        c.append('        OrpheusGenSlot* s = &g_gen_slots[i];')
        c.append('        if (!s->state || s->kind != ORPHEUS_SLOT_PROBE) continue;')
        c.append('        if (seen++ != index) continue;')
        c.append('        if (node) *node = s->node;')
        c.append('        if (key) *key = s->key;')
        c.append('        if (out) orpheus_control_read_slot(s, out);')
        c.append('        return 0;')
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
        c.append('/* 按 ID 标量读写（RW/RR 命令）：经 id→node/key 引用表定位槽。 */')
        c.append('int orpheus_control_set_value_id(uint32_t id, OrpheusValue v) {')
        c.append('    OrpheusGenSlot* s = orpheus_control_find_by_id(id);')
        c.append('    if (!s || s->kind == ORPHEUS_SLOT_PROBE || s->kind == ORPHEUS_SLOT_STATE) return -1;')
        c.append('    return orpheus_control_write_slot(s, v);')
        c.append('}')
        c.append('')
        c.append('int orpheus_control_get_value_id(uint32_t id, OrpheusValue* out) {')
        c.append('    if (!out) return -1;')
        c.append('    OrpheusGenSlot* s = orpheus_control_find_by_id(id);')
        c.append('    if (!s) return -1;')
        c.append('    orpheus_control_read_slot(s, out);')
        c.append('    return 0;')
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
        if (output_dir / "src" / "host_win.c").exists():
            app_sources += " src/host_win.c"
        if (output_dir / "src" / "olink.c").exists():
            app_sources += " src/olink.c"
        for f in sorted((output_dir / "src").glob("orpheus_link_*.c")):
            app_sources += f" src/{f.name}"
        lines.append(f'add_executable(orpheus_generated_app {app_sources})')
        libs = " ".join(self._component_target_name(cid) for cid in component_ids)
        lines.append(f'target_link_libraries(orpheus_generated_app {libs})')
        if (output_dir / "src" / "host_win.c").exists():
            # win 实时宿主：miniaudio 在 Windows 需要的系统库（与 rt_host 一致）
            lines.append('if(WIN32)')
            lines.append('  target_link_libraries(orpheus_generated_app ole32 oleaut32 uuid winmm)')
            lines.append('endif()')
        if self.uart_link_decls(plan):
            # 冒烟 harness 的 stdio 链路默认实现；上设备时移除该定义并实现自己的 send
            lines.append('target_compile_definitions(orpheus_generated_app PRIVATE ORPHEUS_LINK_STDIO)')
            lines.append('if(NOT WIN32)')
            lines.append('  target_link_libraries(orpheus_generated_app pthread)')
            lines.append('endif()')

        with open(output_dir / "CMakeLists.txt", "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
