#!/usr/bin/env python3
"""Build the complete source-grounded dual-runtime Algorithm IR."""

from __future__ import annotations

import argparse
import copy
from pathlib import Path
from typing import Any

import yaml


SOURCES = [
    ("bus", "BVP_Config.yml", "5fd73bdb7b47114fef5c68162a0cf591efb27443b98b6081fa68c12e2e6b0730"),
    ("a_main", "Model_1_1_ert_shrlib_rtw/Model_1_1.c", "266a97f1f43276b7f93d94a2cb35981b6089a4c4905faf7814a9342e532e0889"),
    ("a_post", "Model_1_1_ert_shrlib_rtw/PostProcess.c", "fc6fcd638edd10e0be1d9edc6cda7a617967b885cb227d9cdea7cb5abb581098"),
    ("a_input_select_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_InputSelect_p0_b0_TOP.c", "01790c1e0ab2fc89df5cc8e156477e5b26213d924c1caefccb96f131782dc866"),
    ("a_preamp_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_PreAmp_p0_b0_TOP.c", "f7fe36b16a469f3a333fd8bee974853e19e1b959ce734bed325d9206765c3390"),
    ("a_post_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_PostProcess_p0_b0_TOP.c", "c256d3dd0cf54d11c70a66180cef6aff41c170eceef0dde5bdada4a5fa923f2b"),
    ("a_fdp_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_PreAmp_p3_b0_TOP.c", "d9aefce17f1e85348a3c7e50e65f96c0e840a3491db287de4886995d38481e95"),
    ("a_mix_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_PreAmp_p5_b0_TOP.c", "382dc1eb857529a0438c48021da517519111b2a092a08e0b888eb3b1c588df12"),
    ("a_peripheral_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_PreAmp_p8_b0_TOP.c", "72ab99d6af6e36dbb766aea90064978593cda81ec9582810532b7980116a05de"),
    ("a_holigram_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_PreAmp_p12_b0_TOP.c", "a914ec530e051098eefb9f72050121be2be096c8f981d72360a42cf9f28399ed"),
    ("a_fade_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_PreAmp_p13_b0_TOP.c", "3d537ef5b0a7061578b107c0c275448ab761d42a20bb1c2f65c8c3376586dcf0"),
    ("a_audiopilot_top", "Model_1_1_ert_shrlib_rtw/Model_1_1_PreAmp_p14_b0_TOP.c", "6b30b88071b8e7936ad251a34b6e628b8ee5138a429238aeaa0b8b31d74ec48b"),
    ("b_main", "Model_1_2_ert_shrlib_rtw/Model_1_2.c", "e9560f08055eded66e21e1e2646de64de162665f5a4346e2590ec885fcd5354b"),
    ("b_bands", "Model_1_2_ert_shrlib_rtw/MedusaPart1ResettableSubsystem.c", "4aa61d18e19fcc4b3b8777329de12dfb20981950a1baded11099f49e78810634"),
    ("b_vls", "Model_1_2_ert_shrlib_rtw/VLS.c", "4b7f591a9205491fe9c9eaae2b2aada28c28793e2f2e16a6078923a367f860be"),
    ("b_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_TOP.h", "62ce6d9cc0224456bf6d3766ab5d31eb957e123c17ab19cbfa821ca007c891ec"),
    ("b_bands_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p1_b0_TOP.c", "c1efd762a9cddc74525cce04c80b8a93333872363ca9718d2bd9660add7c1720"),
    ("b_fdp_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p2_b0_TOP.c", "20206e0272e69b706e9d4a9f98792f5859d2c0ca176dcb4a047a0d569a55d908"),
    ("b_mix_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p4_b0_TOP.c", "90ff7f8e4285b76f755991930dd9f72b18c15486fbc36f4c21b1bfe1a05acaef"),
    ("b_control_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p6_b0_TOP.c", "e1db02cbf04bed7685cdf56700a9c7ecc43142e68d4e3bece3e13d2a9ed69d16"),
    ("b_peripheral_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p7_b0_TOP.c", "8c430b29732fccd31905da9a327f639681b027887a16deb7d25d4c671bffa92d"),
    ("b_peripheral_fir", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p7_b1_TOP.c", "d9d992f211935ba77547a5e2357f42d1c24dbea7298db11599a80398a3aa2992"),
    ("b_overhead_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p9_b0_TOP.c", "f51c27693150e72ef51d3a4fc74c4b677db1c1f82e77e4ff21aa6c0e94e7655d"),
    ("b_overhead_fir", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p9_b1_TOP.c", "461f431bebfaf08ef3186850c70e71afce7234dfc3bf1a34ace6f99ab5095e65"),
    ("b_headrest_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p10_b0_TOP.c", "a6f1493856b71081109a0977d69efc252272e1207b081ffdc42c199a5d663d64"),
    ("b_headrest_fir", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p10_b1_TOP.c", "604beb861bab0e1d6aad56bb4565788d360f58ac9f334d62008203e73466eae2"),
    ("b_post_top", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p11_b0_TOP.c", "a75c857b88a4e4744eea4ecbb91705a450070bb32197ba92e7db48f5b2b7678e"),
    ("b_vls_row1_fir", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p11_b2_TOP.c", "5d1ddcc37cee0c5aa7e60579f59c74c8aecf7401a82ff9a170556a2ebe619e86"),
    ("b_vls_row2_fir", "Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p11_b3_TOP.c", "992b655a8e59181ab7643f50621585d290c95cc5a93b3c5687d46770257423af"),
]


def port(port_id: str, type_ref: str, maps_to: str | None = None, meaning: str = "") -> dict[str, Any]:
    result: dict[str, Any] = {"id": port_id, "type_ref": type_ref}
    if maps_to:
        result["maps_to"] = maps_to
    if meaning:
        result["meaning"] = meaning
    return result


def edge(source: str, target: str, type_ref: str, kind: str = "audio", timing: str = "same_call") -> dict[str, Any]:
    return {"from": source, "to": target, "kind": kind, "type_ref": type_ref, "timing": timing}


def matrix_text(rows: int, cols: int, entries: dict[tuple[int, int], float]) -> str:
    values = [entries.get((row, col), 0.0) for row in range(rows) for col in range(cols)]
    return ", ".join(f"{value:g}" for value in values)


def blocker(gate: str, reason: str, source_ref: str) -> dict[str, str]:
    return {"id": f"{gate}_pending", "gate": gate, "reason": reason, "source_ref": source_ref}


def state(state_id: str, role: str, shape: list[int] | None = None, initialization: str = "generated initialization", transitions: list[str] | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {"id": state_id, "role": role, "initialization": initialization}
    if shape:
        result["shape"] = shape
    if transitions:
        result["transitions"] = transitions
    return result


DEFINITIONS: list[dict[str, Any]] = []


def add(
    definition_id: str,
    name: str,
    purpose: str,
    runtime: str,
    tasks: list[str],
    sources: list[str],
    operations: list[str],
    *,
    parent: str | None = None,
    inputs: list[dict[str, Any]] | None = None,
    outputs: list[dict[str, Any]] | None = None,
    children: list[str] | None = None,
    edges: list[dict[str, Any]] | None = None,
    states: list[dict[str, Any]] | None = None,
    branches: list[str] | None = None,
    parameters: list[str] | None = None,
    status: str = "unsupported",
    component: str | None = None,
    lowering: dict[str, Any] | None = None,
    blockers: list[dict[str, str]] | None = None,
) -> None:
    suffix = definition_id.lower().replace(".", "_")
    DEFINITIONS.append({
        "id": definition_id,
        "component_id": f"external_model.index.{suffix}",
        "node_id": definition_id.rsplit(".", 1)[-1].lower() if parent else suffix,
        "parent": parent,
        "name": name,
        "purpose": purpose,
        "runtime_ref": runtime,
        "task_refs": tasks,
        "source_refs": sources,
        "interface": {"inputs": inputs or [], "outputs": outputs or []},
        "operations": operations,
        "branches": branches or [],
        "state": states or [],
        "parameter_refs": parameters or [],
        "children": children or [],
        "body": {"edges": edges or []},
        "implementation": {
            "status": status,
            **({"component": component} if component else {}),
            **({"lowering": lowering} if lowering else {}),
            "blockers": blockers if blockers is not None else [
                blocker("algorithm", "尚未完成源算法逐语句实现", sources[0]),
                blocker("golden", "尚未完成参考输出对拍", sources[0]),
            ],
        },
    })


def build_definitions() -> None:
    input_mixer_514 = matrix_text(10, 12, {
        (0, 0): 1.0, (1, 1): 1.0, (2, 2): 1.0, (3, 3): 3.16,
        (4, 4): 1.0, (4, 6): 1.0, (5, 5): 1.0, (5, 7): 1.0,
        (6, 8): 1.0, (7, 9): 1.0, (8, 10): 1.0, (9, 11): 1.0,
    })
    input_mixer_stereo = matrix_text(10, 12, {
        (0, 0): 1.0, (0, 2): 0.5, (0, 3): 1.58, (0, 4): 1.0,
        (0, 6): 1.0, (0, 8): 1.0, (0, 9): 1.0,
        (1, 1): 1.0, (1, 2): 0.5, (1, 3): 1.58, (1, 5): 1.0,
        (1, 7): 1.0, (1, 10): 1.0, (1, 11): 1.0,
    })
    mix_deci_route_connections = [
        {"from": "center:out0", "to": "peripheral:in0"},
        {"from": "center:out1", "to": "peripheral:in1"},
    ]
    mix_deci_route_connections.extend(
        {"from": f"left:out{index}", "to": f"peripheral:in{index + 2}"}
        for index in range(10)
    )
    mix_deci_route_connections.extend(
        {"from": f"right:out{index}", "to": f"peripheral:in{index + 12}"}
        for index in range(10)
    )
    mix_deci_route_connections.extend(
        {"from": f"left:out{index + 10}", "to": f"overhead:in{index}"}
        for index in range(10)
    )
    mix_deci_route_connections.extend(
        {"from": f"right:out{index + 10}", "to": f"overhead:in{index + 10}"}
        for index in range(10)
    )
    for index in range(5):
        mix_deci_route_connections.extend([
            {"from": f"left:out{index + 20}", "to": f"headrest:in{index * 2}"},
            {"from": f"right:out{index + 20}", "to": f"headrest:in{index * 2 + 1}"},
        ])
    # Runtime A hierarchy.
    add("A.RUNTIME", "Runtime A 全速率系统", "执行输入、Big6、空间处理、输出保护和自适应响度", "runtime_a", ["A0", "A1", "A2", "A3", "A4", "A5"], ["a_main"], ["按 A0..A5 调度并与 Runtime B 交换原子总线"],
        inputs=[port("audio_in", "audio30", "input_select:in"), port("mono", "audio1", "audiopilot:mono"), port("treble_lr", "audio2", "spatial_full:treble_lr"), port("treble_surround", "audio7", "spatial_full:treble_surround"), port("post_spatial", "audio22", "spatial_full:post_spatial")],
        outputs=[port("main_out", "audio22", "postprocess:main_out"), port("adaptive_audio", "audio10", "audiopilot:out")],
        children=["A.INPUT_SELECT", "A.PREAMP", "A.POSTPROCESS", "A.SPATIAL_FULL", "A.AUDIOPILOT"],
        edges=[edge("input_select:entertainment", "preamp:entertainment", "audio12"), edge("input_select:microphone", "preamp:microphone", "audio1"), edge("preamp:post_buffer", "postprocess:preq_buffer", "audio32"), edge("preamp:adaptive", "audiopilot:program", "audio10"), edge("preamp:microphone_out", "audiopilot:microphone", "audio1"), edge("spatial_full:protected", "postprocess:protected", "audio22"), edge("spatial_full:analysis", "audiopilot:fdp_taps", "audio4")])
    add("A.INPUT_SELECT", "输入选择", "30 路系统输入统一重排后提取节目、麦克风和提示音", "runtime_a", ["A0"], ["a_main", "a_input_select_top"], ["TOP 路由表", "非零 RTC 逐项覆盖", "越界通道置零", "提取 12/1/17 路"], parent="A.RUNTIME", inputs=[port("in", "audio30")], outputs=[port("entertainment", "audio12"), port("microphone", "audio1"), port("announcement", "audio17")], states=[state("rtc_route", "30 项运行期覆盖表", [30])], status="verified", lowering={
        "nodes": [
            {"id": "ingress", "component": "orpheus.builtin.gain", "params": {"channels": 30, "gain_db": 0.0}},
            {"id": "route", "component": "orpheus.builtin.input_select", "params": {"channels_in": 30, "channels_out": 30, "select": ",".join(str(index) for index in range(1, 31)), "override_select": ",".join("0" for _ in range(30))}},
            {"id": "entertainment", "component": "orpheus.builtin.input_select", "params": {"channels_in": 30, "channels_out": 12, "select": ",".join(str(index) for index in range(1, 13))}},
            {"id": "microphone", "component": "orpheus.builtin.input_select", "params": {"channels_in": 30, "channels_out": 1, "select": "30"}},
            {"id": "announcement", "component": "orpheus.builtin.input_select", "params": {"channels_in": 30, "channels_out": 17, "select": ",".join(str(index) for index in range(13, 30))}},
        ],
        "connections": [
            {"from": "ingress:out", "to": "route:in"},
            {"from": "route:out", "to": "entertainment:in"},
            {"from": "route:out", "to": "microphone:in"},
            {"from": "route:out", "to": "announcement:in"},
        ],
        "port_maps": {"in": "ingress:in", "entertainment": "entertainment:out", "microphone": "microphone:out", "announcement": "announcement:out"},
        "parameters": [{"id": "router_override", "name": "30路运行期路由覆盖", "type": "string", "default": ",".join("0" for _ in range(30)), "update_policy": "block_boundary", "direction": "input", "maps_to": "route:override_select", "bindable": True}],
    }, blockers=[])
    add("A.PREAMP", "PreAmp 与 Big6", "形成 10 路节目并分出主后处理、自适应和电平检测支路", "runtime_a", ["A0"], ["a_main"], ["12→10 布局", "Volume 后主支路", "Tone/Balance 后自适应支路"], parent="A.RUNTIME",
        inputs=[port("entertainment", "audio12", "input_mixer:in"), port("microphone", "audio1", "route_level:microphone")], outputs=[port("post_buffer", "audio32", "route_level:post_buffer"), port("adaptive", "audio10", "route_level:adaptive"), port("microphone_out", "audio1", "route_level:microphone_out")],
        children=["A.PREAMP.INPUT_MIXER", "A.PREAMP.MUSIC_VOLUME", "A.PREAMP.BASS", "A.PREAMP.TREBLE", "A.PREAMP.MID", "A.PREAMP.BALANCE", "A.PREAMP.ROUTE_LEVEL"],
        edges=[edge("input_mixer:out", "music_volume:in", "audio10"), edge("input_mixer:out", "route_level:program", "audio10"), edge("music_volume:out", "bass:in", "audio10"), edge("music_volume:out", "route_level:volume", "audio10"), edge("bass:out", "treble:in", "audio10"), edge("treble:out", "mid:in", "audio10"), edge("mid:out", "balance:in", "audio10"), edge("balance:out", "route_level:balanced", "audio10")])
    add("A.PREAMP.INPUT_MIXER", "节目布局转换", "12 路 7.1.4 输入并行计算 stereo 与 5.1.4 后选择 10 路输出", "runtime_a", ["A0"], ["a_main", "a_preamp_top"], ["makeup gain", "LFE/环绕加权", "立体声下混", "5.1.4 重排", "模式选择"], parent="A.PREAMP", inputs=[port("in", "audio12")], outputs=[port("out", "audio10")], branches=["stereo 路径通道 3..10 置零", "默认 5.1.4 路径"], status="verified", component="orpheus.builtin.input_mixer_3d", lowering={
        "nodes": [
            {"id": "ingress", "component": "orpheus.builtin.gain", "params": {"channels": 12, "gain_db": 0.0}},
            {"id": "mix_514", "component": "orpheus.builtin.input_mixer_3d", "params": {"input_channels": 12, "output_channels": 10, "gain_db": 0.0, "weights": input_mixer_514}},
            {"id": "mix_stereo", "component": "orpheus.builtin.input_mixer_3d", "params": {"input_channels": 12, "output_channels": 10, "gain_db": 0.0, "weights": input_mixer_stereo}},
            {"id": "mux", "component": "orpheus.builtin.n_way_mux", "params": {"inputs": 2, "channels": 10, "select_index": 0, "ramp_ms": 0.0}},
        ],
        "connections": [
            {"from": "ingress:out", "to": "mix_514:in"},
            {"from": "ingress:out", "to": "mix_stereo:in"},
            {"from": "mix_514:out", "to": "mux:in0"},
            {"from": "mix_stereo:out", "to": "mux:in1"},
        ],
        "port_maps": {"in": "ingress:in", "out": "mux:out"},
        "parameters": [{"id": "stereo_downmix", "name": "立体声下混", "type": "int", "default": 0, "range": [0, 1], "update_policy": "smoothed", "direction": "input", "maps_to": "mux:select_index", "bindable": True}],
    }, blockers=[])
    add("A.PREAMP.MUSIC_VOLUME", "节目延迟与音量", "对 10 路节目做电平检测对齐延迟和单 ramper 无跳变音量", "runtime_a", ["A0"], ["a_main"], ["1120 状态延迟", "8 点索引/dB 插值", "逐样本指数增益", "freeze/snap/current gain"], parent="A.PREAMP", inputs=[port("in", "audio10")], outputs=[port("out", "audio10"), port("gain_state", "control10")], states=[state("music_delay", "10 路延迟状态", [1120]), state("volume_ramper", "current/target/ramp/frame/freeze")], status="approximation", component="orpheus.builtin.gain_ramper")
    add("A.PREAMP.BASS", "低频音调", "按索引在 boost/cut 一阶残差滤波之间切换并跨 unity 两阶段平滑", "runtime_a", ["A0"], ["a_main"], ["一阶滤波", "y=x+(g-1)f(x)", "boost/cut 两阶段 ramper"], parent="A.PREAMP", inputs=[port("in", "audio10")], outputs=[port("out", "audio10")], states=[state("filter", "每通道一阶状态", [10]), state("ramper", "phase/frame/current/target/freeze")], status="approximation", component="orpheus.builtin.bass")
    add("A.PREAMP.TREBLE", "高频音调", "按索引在 boost/cut 一阶残差滤波之间切换并平滑", "runtime_a", ["A0"], ["a_main"], ["一阶滤波", "残差增益", "两阶段 ramper"], parent="A.PREAMP", inputs=[port("in", "audio10")], outputs=[port("out", "audio10")], states=[state("filter", "每通道一阶状态", [10])], status="approximation", component="orpheus.builtin.treble")
    add("A.PREAMP.MID", "中频音调", "使用二阶残差滤波和两阶段增益斜坡调节中频", "runtime_a", ["A0"], ["a_main"], ["二阶滤波", "使用 numerator 1/3 项", "残差增益"], parent="A.PREAMP", inputs=[port("in", "audio10")], outputs=[port("out", "audio10")], states=[state("filter", "每通道二阶状态", [20])], status="approximation", component="orpheus.builtin.midrange")
    add("A.PREAMP.BALANCE", "四组 Balance", "按 L/R/Center/Mono 四组映射平滑调整 10 路声像", "runtime_a", ["A0"], ["a_main", "a_preamp_top"], ["30 点 dB 表", "mean(L/R) 中置与低音", "极端静音", "四 ramper"], parent="A.PREAMP", inputs=[port("in", "audio10")], outputs=[port("out", "audio10")], states=[state("rampers", "四组 current/target/ramp/frame/freeze", [4])], status="verified", component="orpheus.builtin.sleeping_beauty", lowering={
        "component": "orpheus.builtin.sleeping_beauty",
        "params": {
            "channels": 10,
            "gain_index": 128.0,
            "offset": 128.0,
            "mutes_bass": 1.0,
            "ramp_ms": 30.0,
            "ramp_db_per_s": 0.0,
            "freeze": False,
            "center_mono_mode": "mean_lr",
            "chan_map": "0,1,2,3,0,1,0,0,1,1",
            "table_idx": "0,10,31,52,74,95,116,128,138,159,180,202,223,244,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255",
            "table_db": "-30,-27,-14,-7,-5,-3,-1,0,-1,-3,-5,-7,-14,-27,-30,-30,-30,-30,-30,-30,-30,-30,-30,-30,-30,-30,-30,-30,-30,-30",
        },
        "parameters": [
            {"id": "gain_index", "name": "Balance 索引", "type": "float", "default": 128.0, "range": [0.0, 255.0], "update_policy": "smoothed", "direction": "input", "maps_to": "impl:gain_index", "bindable": True},
            {"id": "ramp_db_per_s", "name": "斜坡速率", "type": "float", "default": 0.0, "range": [0.0, 50000.0], "update_policy": "block_boundary", "direction": "input", "maps_to": "impl:ramp_db_per_s", "bindable": True},
            {"id": "ramp_ms", "name": "斜坡时间", "type": "float", "default": 30.0, "range": [1.0, 3000.0], "update_policy": "block_boundary", "direction": "input", "maps_to": "impl:ramp_ms", "bindable": True},
            {"id": "freeze", "name": "冻结斜坡", "type": "bool", "default": False, "update_policy": "block_boundary", "direction": "input", "maps_to": "impl:freeze", "bindable": True},
        ],
    }, blockers=[])
    add("A.PREAMP.ROUTE_LEVEL", "PreAmp 分支与电平检测", "从 Volume、Balance、Mic 和 InputMixer 分出主输出、自适应和电平状态", "runtime_a", ["A0"], ["a_main"], ["Volume→32 路仅源 1..6", "Balance→10 路 adaptive", "Mic→Buffer2", "预加重/RMS/Peak/InnerLink"], parent="A.PREAMP", inputs=[port("program", "audio10"), port("volume", "audio10"), port("balanced", "audio10"), port("microphone", "audio1")], outputs=[port("post_buffer", "audio32"), port("adaptive", "audio10"), port("microphone_out", "audio1"), port("levels", "control2")], states=[state("preemphasis", "10 路 IIR", [20]), state("inner_links", "AP/DynEQ hold/decay 状态", [2])], status="unsupported")
    add("A.POSTPROCESS", "22 路输出后处理", "将保护链和 32 路候选路由为最终 22 路输出", "runtime_a", ["A0"], ["a_main", "a_post"], ["内部保护", "主辅路由", "校准/FreqComp/延迟"], parent="A.RUNTIME", inputs=[port("protected", "audio22", "protected:in"), port("preq_buffer", "audio32", "router_output:preq"), port("announcement", "audio17", "router_output:announcement")], outputs=[port("main_out", "audio22", "router_output:out")], children=["A.POSTPROCESS.PROTECTED", "A.POSTPROCESS.ROUTER_OUTPUT"], edges=[edge("protected:out", "router_output:protected", "audio22")])
    add("A.POSTPROCESS.PROTECTED", "Limiter/PostEQ/内部软削波", "对 Part6 保护输入执行增益、限制、EQ、软削波和 RMDL mute", "runtime_a", ["A0"], ["a_post"], ["22 ramper", "BDSP limiter", "PostEQ", "piecewise soft clip", "RMDL mute"], parent="A.POSTPROCESS", inputs=[port("in", "audio22")], outputs=[port("out", "audio22")], states=[state("limiter_gain", "每通道递归限制增益", [22]), state("posteq", "22 路 IIR")], status="approximation", component="orpheus.builtin.limiter")
    add("A.POSTPROCESS.ROUTER_OUTPUT", "ASD 路由与输出补偿", "从 71 路候选加性选择 22 路并完成输出保护、校准、频补和延迟", "runtime_a", ["A0"], ["a_main"], ["22 main select/gain", "aux additive select/gain", "all mute", "第二级 soft clip", "OutputCal", "FreqComp", "pool delay"], parent="A.POSTPROCESS", inputs=[port("protected", "audio22"), port("preq", "audio32"), port("announcement", "audio17")], outputs=[port("out", "audio22")], states=[state("output_cal", "运行期 22 路校准", [22]), state("freq_comp", "22 路 IIR"), state("delay", "22 路可调延迟")], status="unsupported")
    add("A.SPATIAL_FULL", "Runtime A 空间链", "把 Runtime B 高频素材转换为 22 路全速率空间结果并与降速结果合成", "runtime_a", ["A0", "A2"], ["a_main"], ["FDP", "13→22 矩阵", "外围 EQ", "全息延迟", "Fade/求和"], parent="A.RUNTIME", inputs=[port("treble_lr", "audio2", "fdp:in"), port("treble_surround", "audio7", "mix:surround"), port("post_spatial", "audio22", "fade_sum:deci")], outputs=[port("protected", "audio22", "fade_sum:out"), port("analysis", "audio4", "fdp:analysis")], children=["A.SPATIAL_FULL.FDP", "A.SPATIAL_FULL.MIX", "A.SPATIAL_FULL.PERIPHERAL_EQ", "A.SPATIAL_FULL.POST_HOLOGRAM", "A.SPATIAL_FULL.FADE_SUM"], edges=[edge("fdp:spatial", "mix:fdp", "audio6"), edge("mix:out", "peripheral_eq:in", "audio22"), edge("peripheral_eq:out", "post_hologram:in", "audio22"), edge("post_hologram:out", "fade_sum:full", "audio22")])
    add("A.SPATIAL_FULL.FDP", "全速率 2→6 FDP", "用 WOLA 与残差/混响估计把两路高频分解为六路空间成分", "runtime_a", ["A0", "A2"], ["a_main", "a_fdp_top"], ["跨 TID ring", "256 RFFT", "系数平滑", "冲击/尾音分离", "IFFT overlap-add"], parent="A.SPATIAL_FULL", inputs=[port("in", "audio2", "transport:in")], outputs=[port("spatial", "audio6", "transport:out"), port("analysis", "audio4", "transport:analysis")], children=["A.SPATIAL_FULL.FDP.TRANSPORT", "A.SPATIAL_FULL.FDP.ANALYSIS", "A.SPATIAL_FULL.FDP.COEFFICIENTS", "A.SPATIAL_FULL.FDP.REVERB", "A.SPATIAL_FULL.FDP.SYNTHESIS"], edges=[edge("transport:frame", "analysis:in", "audio2_128"), edge("analysis:spectrum", "coefficients:spectrum", "spectrum2", "spectrum"), edge("coefficients:residual", "reverb:residual", "spectrum2", "spectrum"), edge("coefficients:direct", "synthesis:direct", "spectrum2", "spectrum"), edge("reverb:split", "synthesis:reverb", "spectrum4", "spectrum")])
    add("A.SPATIAL_FULL.FDP.TRANSPORT", "FDP 多速率缓冲", "A0 写 2×32/读 6×32，A2 读 2×128/写 6×128", "runtime_a", ["A0", "A2"], ["a_main"], ["512 输入 ring", "1536 输出 ring", "保持读写指针和初始相位"], parent="A.SPATIAL_FULL.FDP", inputs=[port("in", "audio2")], outputs=[port("frame", "audio2_128"), port("out", "audio6"), port("analysis", "audio4")], states=[state("rings", "跨 TID 输入/输出 ring", [2048])], status="unsupported")
    add("A.SPATIAL_FULL.FDP.ANALYSIS", "FDP Analysis WOLA", "将前后两个 128 样本半帧加窗并做双通道 256 点 RFFT", "runtime_a", ["A2"], ["a_main"], ["sin/cos 窗", "256 RFFT", "129 complex bins"], parent="A.SPATIAL_FULL.FDP", inputs=[port("in", "audio2_128")], outputs=[port("spectrum", "spectrum2")], states=[state("overlap", "前半帧", [256])], status="unsupported")
    add("A.SPATIAL_FULL.FDP.COEFFICIENTS", "FDP 直达/残差系数", "估计 far/near 与互补残差并用功率加权最小二乘平滑", "runtime_a", ["A2"], ["a_main"], ["幅度最小值", "far-only", "互补残差", "8 个 129-bin smoother"], parent="A.SPATIAL_FULL.FDP", inputs=[port("spectrum", "spectrum2")], outputs=[port("direct", "spectrum2"), port("residual", "spectrum2")], states=[state("smoothers", "xLast/yLast", [8, 129])], status="unsupported")
    add("A.SPATIAL_FULL.FDP.REVERB", "FDP 混响/冲击分离", "以快慢 PSD、冲击保持和延迟谱把残差拆成 impulse/tail", "runtime_a", ["A2"], ["a_main", "a_fdp_top"], ["fast/slow PSD", "1.5dB 冲击检测", "延迟 PSD", "DecayRate 缩放", "LS smoothing"], parent="A.SPATIAL_FULL.FDP", inputs=[port("residual", "spectrum2")], outputs=[port("split", "spectrum4")], states=[state("spectral_history", "四组 2193 元素延迟", [4, 2193]), state("impulse", "双侧计数/保持", [2])], status="unsupported")
    add("A.SPATIAL_FULL.FDP.SYNTHESIS", "FDP Synthesis WOLA", "把两路直达和四路 impulse/tail 复谱 IFFT、加窗并 overlap-add 为 6×128", "runtime_a", ["A2"], ["a_main"], ["6 路 256 IFFT", "synthesis window", "128 overlap-add"], parent="A.SPATIAL_FULL.FDP", inputs=[port("direct", "spectrum2"), port("reverb", "spectrum4")], outputs=[port("out", "audio6_128")], states=[state("overlap", "六路输出半帧", [768])], status="unsupported")
    add("A.SPATIAL_FULL.MIX", "13→22 全速率混音", "组织 FDP6 与 surround7，并以 26/70/70 三矩阵平滑生成 22 路", "runtime_a", ["A0"], ["a_main", "a_mix_top"], ["surround delay/派生", "13 路 MixEQ", "CS/Left/Right 输入组织", "sequence 矩阵 ramp"], parent="A.SPATIAL_FULL", inputs=[port("fdp", "audio6", "organize:fdp"), port("surround", "audio7", "organize:surround"), port("targets", "gains166", "ramp_control:targets")], outputs=[port("out", "audio22", "ramp_control:out"), port("current_gains", "gains166", "ramp_control:current")], children=["A.SPATIAL_FULL.MIX.ORGANIZE", "A.SPATIAL_FULL.MIX.RAMP_CONTROL"], edges=[edge("organize:organized", "ramp_control:audio", "audio13")])
    add("A.SPATIAL_FULL.MIX.ORGANIZE", "空间混音输入组织", "延迟并组合 FDP/环绕，形成 CS13 与 Left/Right7 的有序输入", "runtime_a", ["A0"], ["a_main"], ["7 路延迟", "Ltf+Ltb/Rtf+Rtb", "13 路 per-channel IIR", "三组明确顺序"], parent="A.SPATIAL_FULL.MIX", inputs=[port("fdp", "audio6"), port("surround", "audio7")], outputs=[port("organized", "audio13")], states=[state("surround_delay", "7 路 1664 样本延迟", [30436]), state("mix_eq", "13 路 IIR", [260])], status="unsupported")
    add("A.SPATIAL_FULL.MIX.RAMP_CONTROL", "26/70/70 矩阵斜坡", "按 FastSequence 冻结/启动目标，逐样本平滑三矩阵并回传当前增益", "runtime_a", ["A0"], ["a_main", "bus"], ["偶序列接受目标", "奇序列冻结", "零值微扰", "CS13→2/Left7→10/Right7→10"], parent="A.SPATIAL_FULL.MIX", inputs=[port("audio", "audio13"), port("targets", "gains166")], outputs=[port("out", "audio22"), port("current", "gains166")], states=[state("matrices", "current/target/coeff/sequence", [166])], status="approximation", component="orpheus.builtin.slc_matrix_mul")
    add("A.SPATIAL_FULL.PERIPHERAL_EQ", "22 路外围 EQ", "重排扬声器顺序、每通道 IIR，并施加固定 254 样本延迟", "runtime_a", ["A0"], ["a_main", "a_peripheral_top"], ["22 路重排", "accelerator IIR", "254 样本固定延迟", "disable=zero"], parent="A.SPATIAL_FULL", inputs=[port("in", "audio22")], outputs=[port("out", "audio22")], states=[state("iir", "22 路滤波状态"), state("delay", "22×254 延迟")], status="unsupported")
    add("A.SPATIAL_FULL.POST_HOLOGRAM", "全速率全息后处理", "四组 SleepingBeauty 增益后施加 22 路相位对齐延迟", "runtime_a", ["A0"], ["a_main", "a_holigram_top"], ["四组表插值", "指数 ramper", "-1 通道置零", "phase delay"], parent="A.SPATIAL_FULL", inputs=[port("in", "audio22")], outputs=[port("out", "audio22")], states=[state("rampers", "四组 current/target/frame/freeze", [4]), state("delay", "22×80 延迟", [1760])], status="approximation", component="orpheus.builtin.sleeping_beauty")
    add("A.SPATIAL_FULL.FADE_SUM", "22 路求和/Fade/Mute", "合并全速率与降速结果，执行扬声器延迟、路由、频分 Fade 和 Mute", "runtime_a", ["A0"], ["a_main", "a_fade_top", "bus"], ["22 路相加", "SpeakerDelay", "OutputRouter", "LP/HP Fade 四 ramper", "global mute", "导出两个非低频 target"], parent="A.SPATIAL_FULL", inputs=[port("full", "audio22"), port("deci", "audio22")], outputs=[port("out", "audio22"), port("fade_targets", "control2")], states=[state("speaker_delay", "22 路延迟", [32384]), state("fade", "四组 ramper 与 LPF", [4, 22]), state("mute", "全局 ramper")], status="approximation", component="orpheus.builtin.fade")
    add("A.AUDIOPILOT", "多速率自适应响度", "估计 HF/LF 噪声和信号比，计算多频段补偿并施加到 10 路节目", "runtime_a", ["A0", "A1", "A3", "A4", "A5"], ["a_main", "a_audiopilot_top"], ["HF 预处理", "HF 相干/ratio", "LF Welch/冲击", "BoostMap/增益应用"], parent="A.RUNTIME", inputs=[port("program", "audio10", "gain_application:program"), port("microphone", "audio1", "hf_preprocess:microphone"), port("mono", "audio1", "lf:reference"), port("fdp_taps", "audio4", "hf_preprocess:fdp")], outputs=[port("out", "audio10", "gain_application:out"), port("metrics", "control4", "gain_application:metrics")], children=["A.AUDIOPILOT.HF_PREPROCESS", "A.AUDIOPILOT.HF_ESTIMATOR", "A.AUDIOPILOT.LF", "A.AUDIOPILOT.GAIN_APPLICATION"], edges=[edge("hf_preprocess:frames", "hf_estimator:frames", "audio10_2"), edge("hf_estimator:metrics", "gain_application:hf_metrics", "control4", "control"), edge("lf:metrics", "gain_application:lf_metrics", "control4", "control")])
    add("A.AUDIOPILOT.HF_PREPROCESS", "HF 噪声预处理", "组合 mic/FDP/surround 参考，滤波、抽取并写入十通道 HF ring", "runtime_a", ["A0"], ["a_main", "a_audiopilot_top"], ["BP/LP IIR", "11 项周期调制", "AA IIR", "÷16", "参考对齐"], parent="A.AUDIOPILOT", inputs=[port("microphone", "audio1"), port("fdp", "audio4")], outputs=[port("frames", "audio10_2")], states=[state("hf_ring", "10×512 ring", [5120]), state("reference_delay", "参考对齐", [5418])], status="unsupported")
    add("A.AUDIOPILOT.HF_ESTIMATOR", "HF 相干/噪声/Ratio", "对十通道 FFT 协方差做消元相干估计、min-stat 噪声和 LF/HF ratio", "runtime_a", ["A3"], ["a_main", "a_audiopilot_top"], ["256 RFFT", "65×10×10 complex covariance", "Gaussian elimination", "min-stat 三窗口", "HF/ratio slew"], parent="A.AUDIOPILOT", inputs=[port("frames", "audio10_2")], outputs=[port("metrics", "control4")], states=[state("covariance", "65×10×10 complex", [6500]), state("minima", "三窗口频谱最小值", [195])], status="unsupported")
    add("A.AUDIOPILOT.LF", "LF Welch/冲击估计", "跨 A0/A1/A4/A5 估计低频参考、噪声、音乐和速度约束", "runtime_a", ["A0", "A1", "A4", "A5"], ["a_main", "a_audiopilot_top"], ["mic/ref IIR", "750Hz ring", "FFT256 hop128 bins5..34", "Cross/Ref/Mic PSD K=3", "coherence/variance/gamma", "speed/HVAC slew"], parent="A.AUDIOPILOT", inputs=[port("reference", "audio1")], outputs=[port("metrics", "control4")], states=[state("rings", "mic/ref/overlap ring"), state("psd", "30 bin spectra and counters", [30])], status="unsupported")
    add("A.AUDIOPILOT.GAIN_APPLICATION", "BoostMap 与增益应用", "由噪声/信号/用户级计算 Bass/Mid/Treble/DynEQ 增益并作用于 10 路", "runtime_a", ["A0"], ["a_main", "a_audiopilot_top"], ["BoostMap 插值", "alpha/beta/ratio", "downward expansion", "enable ramp", "三 anti-zipper", "direct/LPF/BPF 并行求和"], parent="A.AUDIOPILOT", inputs=[port("program", "audio10"), port("hf_metrics", "control4"), port("lf_metrics", "control4")], outputs=[port("out", "audio10"), port("metrics", "control4")], states=[state("filters", "BPF/LPF/zipper/align"), state("boost", "平滑 boost 状态")], status="unsupported")

    # Runtime B hierarchy.
    add("B.RUNTIME", "Runtime B 降速空间系统", "计算频带、降速 FDP、目标矩阵、HEQ 和 VLS 后空间结果", "runtime_b", ["B0", "B1", "B2", "B3", "B4"], ["b_main"], ["4500Hz 基准调度", "B1/B2/B3/B4 多速率处理", "跨 Runtime 音频字段导出为标准音频端口，控制字段保留事务合同"],
        inputs=[port("adaptive_audio", "audio10", "bands:adaptive")],
        outputs=[port("mono", "audio1", "bands:mono"), port("treble_lr", "audio2", "bands:treble_lr"), port("treble_surround", "audio7", "bands:treble_surround"), port("post_spatial", "audio22", "post:out")],
        children=["B.BANDS", "B.FDP", "B.MIX_CONTROL", "B.MIX_DECI", "B.HEQ", "B.POST"], edges=[edge("bands:treble_lr", "fdp:in", "audio2"), edge("fdp:components", "mix_deci:fdp", "audio8"), edge("bands:bass_mid", "mix_deci:bands", "audio4"), edge("mix_control:deci_targets", "mix_deci:targets", "gains1134", "control"), edge("mix_control:vls_row1", "post:row1_targets", "gains40", "control"), edge("mix_control:vls_row2", "post:row2_targets", "gains40", "control"), edge("mix_deci:peripheral", "heq:peripheral", "audio22"), edge("mix_deci:headrest", "heq:headrest", "audio10"), edge("mix_deci:overhead", "heq:overhead", "audio20"), edge("heq:peripheral_out", "post:peripheral", "audio10"), edge("heq:headrest_out", "post:headrest", "audio40"), edge("heq:overhead_out", "post:overhead", "audio4")])
    add("B.BANDS", "频带与 SRC", "从 A 的十路自适应音频生成 mono、treble、surround、bass/mid 与 decay", "runtime_b", ["B1", "B2", "B3"], ["b_main", "b_bands", "b_bands_top"], ["3/13 rational SRC", "stereo detector", "decay estimator", "mono weighted sum", "bass/mid filters"], parent="B.RUNTIME", inputs=[port("adaptive", "audio10", "src:in")], outputs=[port("mono", "audio1", "mono:out"), port("treble_lr", "audio2", "src:treble_lr"), port("treble_surround", "audio7", "src:surround"), port("bass_mid", "audio4", "bass:out"), port("decay", "control1", "decay:out")], children=["B.BANDS.SRC", "B.BANDS.MONO_DETECTOR", "B.BANDS.DECAY", "B.BANDS.MONO", "B.BANDS.BASS"], edges=[edge("src:treble_lr", "mono_detector:in", "audio2"), edge("src:treble_lr", "decay:in", "audio2"), edge("src:deci", "mono:in", "audio10"), edge("src:deci", "bass:in", "audio10")])
    add("B.BANDS.SRC", "3/13 Rational SRC", "把 10 路 48k 内容变换到 B3 域，并在 B1 重构 treble/surround", "runtime_b", ["B1", "B3"], ["b_main", "b_bands"], ["3 相下采样 348 系数", "13 相上采样 351 系数", "phase 4/4/5", "重构残差"], parent="B.BANDS", inputs=[port("in", "audio10")], outputs=[port("deci", "audio10"), port("treble_lr", "audio2"), port("surround", "audio7")], states=[state("src_rings", "alignment/down/up buffers", [9270])], status="unsupported")
    add("B.BANDS.MONO_DETECTOR", "Stereo/Mono 检测", "由两路 HPF 电平与能量关系做 silence reset 和 stereo hysteresis", "runtime_b", ["B1"], ["b_bands"], ["2 路 HPF", "LR mismatch saturation", "energy ratio smoothing", "hysteresis"], parent="B.BANDS", inputs=[port("in", "audio2")], outputs=[port("gate", "control1")], states=[state("detector", "level/energy/counter/last gate")], status="unsupported")
    add("B.BANDS.DECAY", "DecayRate 估计", "B1 滤波缓冲、B2 能量/起始检测和自适应 slew 后发布 decay", "runtime_b", ["B1", "B2"], ["b_bands", "b_bands_top"], [">6k HPF", "32→128 buffer", "8 帧历史", "onset state machine", "bounded slew"], parent="B.BANDS", inputs=[port("in", "audio2")], outputs=[port("out", "control1")], states=[state("energy_history", "八帧能量", [8]), state("onset", "on/off/counter/slew")], status="unsupported")
    add("B.BANDS.MONO", "Mono 频带", "左右各六路加权后求平均得到 mono", "runtime_b", ["B1"], ["b_bands"], ["BassManager 权重", "双六输入求和", "左右平均"], parent="B.BANDS", inputs=[port("in", "audio10")], outputs=[port("out", "audio1")], status="verified", component="orpheus.builtin.matrix_mul", lowering={"component": "orpheus.builtin.matrix_mul", "params": {"rows": 1, "cols": 10, "matrix": "0.5, 0.5, 0, 0, 0, 0, 0, 0, 0, 0"}}, blockers=[])
    add("B.BANDS.BASS", "Bass/Mid 频带", "重构 C/LFE、九路 HPF 和双六输入 LR 求和后 LPF", "runtime_b", ["B3"], ["b_bands"], ["C+=gain*LFE", "移除 C/LFE", "9 路 HPF", "LR weighted sum", "2 路 LPF"], parent="B.BANDS", inputs=[port("in", "audio10")], outputs=[port("out", "audio4")], states=[state("filters", "HPF90 + LPF20", [110])], status="unsupported")
    add("B.FDP", "降速 2→8 FDP", "在 B3/B4 以 WOLA 将两路拆为八路直达、空间、冲击和尾音", "runtime_b", ["B3", "B4"], ["b_main", "b_fdp_top"], ["跨 TID ring", "STFT", "空间系数", "混响分离", "八路 WOLA"], parent="B.RUNTIME", inputs=[port("in", "audio2", "buffer:in")], outputs=[port("components", "audio8", "buffer:out")], children=["B.FDP.BUFFER", "B.FDP.STFT", "B.FDP.COEFFICIENTS", "B.FDP.REVERB", "B.FDP.WOLA"], edges=[edge("buffer:frame", "stft:in", "audio2_128"), edge("stft:spectrum", "coefficients:spectrum", "spectrum2", "spectrum"), edge("coefficients:residual", "reverb:residual", "spectrum2", "spectrum"), edge("coefficients:direct", "wola:direct", "spectrum4", "spectrum"), edge("reverb:split", "wola:reverb", "spectrum4", "spectrum")])
    add("B.FDP.BUFFER", "降速 FDP ring", "B3 写 2×32/读 8×32，B4 读 2×128/写 8×128", "runtime_b", ["B3", "B4"], ["b_main"], ["512 输入 ring", "2048 输出 ring", "持久指针和初始 offset"], parent="B.FDP", inputs=[port("in", "audio2")], outputs=[port("frame", "audio2_128"), port("out", "audio8")], states=[state("rings", "输入输出多速率 ring", [2560])], status="unsupported")
    add("B.FDP.STFT", "降速 FDP STFT", "前后 128 半帧加 sin/cos 窗并执行 256 点双路 RFFT", "runtime_b", ["B4"], ["b_main"], ["overlap", "window", "RFFT 129 bins"], parent="B.FDP", inputs=[port("in", "audio2_128")], outputs=[port("spectrum", "spectrum2")], states=[state("overlap", "输入半帧", [256])], status="unsupported")
    add("B.FDP.COEFFICIENTS", "降速 FDP 系数控制", "计算 Lo/Ro/Ls/Rs 和残差的功率加权平滑系数", "runtime_b", ["B4"], ["b_main", "b_fdp_top"], ["direct/excess", "SPS", "8 个 129-bin LS smoother"], parent="B.FDP", inputs=[port("spectrum", "spectrum2")], outputs=[port("direct", "spectrum4"), port("residual", "spectrum2")], states=[state("smoothers", "8×129 x/y history", [8, 129])], status="unsupported")
    add("B.FDP.REVERB", "降速冲击/尾音分离", "用快慢 PSD、冲击计数、延迟谱和 DecayRate 分离 impulse/tail", "runtime_b", ["B4"], ["b_main", "b_fdp_top"], ["fast/slow PSD", "scalar impulse detector", "16×129 spectral delay", "ratio smoothing"], parent="B.FDP", inputs=[port("residual", "spectrum2")], outputs=[port("split", "spectrum4")], states=[state("spectral_delay", "16×129 history", [16, 129]), state("alignment", "4×774 rings", [4, 774])], status="unsupported")
    add("B.FDP.WOLA", "八路 Synthesis WOLA", "四路直达/空间与四路 impulse/tail 经 IFFT/window/overlap-add 合成八路", "runtime_b", ["B4"], ["b_main"], ["8×IFFT256", "synthesis window", "顺序 Lo,Ro,Ls,Rs,Limp,Ltail,Rimp,Rtail"], parent="B.FDP", inputs=[port("direct", "spectrum4"), port("reverb", "spectrum4")], outputs=[port("out", "audio8_128")], states=[state("overlap", "八路 128 样本", [1024])], status="unsupported")
    add("B.MIX_CONTROL", "目标矩阵与序列控制", "由 A 当前增益/Fade 生成全速率、降速和 VLS 目标并原子发布 sequence", "runtime_b", ["B1", "B3"], ["b_main", "b_control_top", "bus"], ["SLC 三锚插值", "Fade downmix", "奇偶序列 groom", "current/target feedback"], parent="B.RUNTIME", inputs=[port("current_gains", "gains166", "targets:current"), port("fade", "control2", "targets:fade")], outputs=[port("full_targets", "gains166", "targets:full"), port("deci_targets", "gains1134", "targets:deci"), port("vls_row1", "gains40", "targets:vls_row1"), port("vls_row2", "gains40", "targets:vls_row2"), port("sequence", "control1", "sequence:out")], children=["B.MIX_CONTROL.TARGETS", "B.MIX_CONTROL.SEQUENCE", "B.MIX_CONTROL.FEEDBACK"], edges=[])
    add("B.MIX_CONTROL.TARGETS", "SLC/Downmix 目标计算", "对七组矩阵做 index 插值和 Fade 前后向 downmix 混合", "runtime_b", ["B1", "B3"], ["b_main", "b_control_top"], ["0/128/255 anchors", "linear/dB-linear interpolation", "target=(1-d)*slc+d*weights"], parent="B.MIX_CONTROL", inputs=[port("current", "gains166"), port("fade", "control2")], outputs=[port("full", "gains166"), port("deci", "gains1134"), port("vls_row1", "gains40"), port("vls_row2", "gains40")], states=[state("targets", "七组目标矩阵")], status="unsupported")
    add("B.MIX_CONTROL.SEQUENCE", "奇偶序列事务", "奇数冻结、偶数启动，带延迟启动与 FastSequence 返回", "runtime_b", ["B1", "B3"], ["b_main"], ["requested parity", "超过128减128", "freeze immediate", "delayed even launch"], parent="B.MIX_CONTROL", inputs=[port("trigger", "event1")], outputs=[port("out", "control1")], states=[state("sequence", "generator/previous/delay counters")], status="unsupported")
    add("B.MIX_CONTROL.FEEDBACK", "目标/当前增益快照", "在消费者执行后更新 26/70/70、34/525/525/80 的跨任务快照", "runtime_b", ["B1", "B3"], ["b_main", "bus"], ["B1/B3 unit delays", "1e-3 completion", "task-end update"], parent="B.MIX_CONTROL", inputs=[port("active", "gains1134")], outputs=[port("snapshot", "gains1134")], states=[state("snapshots", "任务边界数组快照")], status="unsupported")
    add("B.MIX_DECI", "降速 21→52 混音", "延迟/滤波并组织 21 路，再用 34/525/525 三矩阵生成 52 路 HEQ 输入", "runtime_b", ["B3"], ["b_main", "b_mix_top"], ["alignment", "organizer", "sequence-gated ramp", "peripheral/overhead/headrest route"], parent="B.RUNTIME", inputs=[port("fdp", "audio8", "align:fdp"), port("bands", "audio4", "align:bands"), port("targets", "gains1134", "ramp:targets")], outputs=[port("peripheral", "audio22", "route:peripheral"), port("headrest", "audio10", "route:headrest"), port("overhead", "audio20", "route:overhead"), port("active", "gains1134", "ramp:active")], children=["B.MIX_DECI.ALIGN", "B.MIX_DECI.ORGANIZE", "B.MIX_DECI.RAMP", "B.MIX_DECI.ROUTE"], edges=[edge("align:aligned", "organize:in", "audio21"), edge("organize:cs", "ramp:cs", "audio17"), edge("organize:lr", "ramp:lr", "audio21"), edge("ramp:center", "route:center", "audio2"), edge("ramp:left", "route:left", "audio25"), edge("ramp:right", "route:right", "audio25")])
    add("B.MIX_DECI.ALIGN", "降速延迟/Wiggle/MixEQ", "对 bass/FDP/surround 做多组延迟、四个 FirEven wiggle 和八路 MixEQ", "runtime_b", ["B3"], ["b_main", "b_mix_top"], ["bass384", "FDP/tail alignment", "4×442 FIR", "8 路 IIR", "surround384"], parent="B.MIX_DECI", inputs=[port("fdp", "audio8"), port("bands", "audio4")], outputs=[port("aligned", "audio21")], states=[state("delays", "多组 ring"), state("wiggle", "1904 FIR state"), state("mix_iir", "160 状态")], status="unsupported")
    add("B.MIX_DECI.ORGANIZE", "21 路矩阵输入组织", "按明确索引生成 CS17 与 Left/Right21 输入", "runtime_b", ["B3"], ["b_main"], ["CS 排除 wiggle tails", "LR 共享 21 输入顺序"], parent="B.MIX_DECI", inputs=[port("in", "audio21")], outputs=[port("cs", "audio17"), port("lr", "audio21")], status="verified", component="orpheus.builtin.channel_router", lowering={
        "nodes": [
            {"id": "ingress", "component": "orpheus.builtin.gain", "params": {"channels": 21, "gain_db": 0.0}},
            {"id": "cs", "component": "orpheus.builtin.channel_router", "params": {"channels_in": 21, "channels_out": 17, "indices": "0,2,4,6,8,15,17,18,1,3,5,7,9,16,19,20,14"}},
            {"id": "lr", "component": "orpheus.builtin.channel_router", "params": {"channels_in": 21, "channels_out": 21, "indices": "0,2,4,6,8,10,12,15,17,18,1,3,5,7,9,11,13,16,19,20,14"}},
        ],
        "connections": [{"from": "ingress:out", "to": "cs:in"}, {"from": "ingress:out", "to": "lr:in"}],
        "port_maps": {"in": "ingress:in", "cs": "cs:out", "lr": "lr:out"},
    }, blockers=[])
    add("B.MIX_DECI.RAMP", "34/525/525 矩阵斜坡", "序列锁存三个目标矩阵并逐样本生成 Center2、Left25、Right25", "runtime_b", ["B3"], ["b_main", "b_control_top"], ["g=(1-c)target+c*g", "freeze 仍输出", "偶序列锁存", "零值微扰"], parent="B.MIX_DECI", inputs=[port("cs", "audio17"), port("lr", "audio21"), port("targets", "gains1134")], outputs=[port("center", "audio2"), port("left", "audio25"), port("right", "audio25"), port("active", "gains1134")], states=[state("matrices", "active/target/coeff/sequence", [1134])], status="unsupported")
    add("B.MIX_DECI.ROUTE", "三组矩阵输出 HEQ 分发", "把 Center2、Left25、Right25 分成 peripheral22、overhead20、headrest10", "runtime_b", ["B3"], ["b_main"], ["peripheral selector", "overhead selector", "headrest selector"], parent="B.MIX_DECI", inputs=[port("center", "audio2"), port("left", "audio25"), port("right", "audio25")], outputs=[port("peripheral", "audio22"), port("headrest", "audio10"), port("overhead", "audio20")], status="verified", lowering={
        "nodes": [
            {"id": "center", "component": "orpheus.builtin.deinterleave", "params": {"channels": 2}},
            {"id": "left", "component": "orpheus.builtin.deinterleave", "params": {"channels": 25}},
            {"id": "right", "component": "orpheus.builtin.deinterleave", "params": {"channels": 25}},
            {"id": "peripheral", "component": "orpheus.builtin.interleave", "params": {"channels": 22}},
            {"id": "headrest", "component": "orpheus.builtin.interleave", "params": {"channels": 10}},
            {"id": "overhead", "component": "orpheus.builtin.interleave", "params": {"channels": 20}},
        ],
        "connections": mix_deci_route_connections,
        "port_maps": {"center": "center:in", "left": "left:in", "right": "right:in", "peripheral": "peripheral:out", "headrest": "headrest:out", "overhead": "overhead:out"},
    }, blockers=[])
    add("B.HEQ", "三分支 HEQ", "并行计算 peripheral/headrest/overhead 的 FIR、延迟输入 IIR、求和和输出环", "runtime_b", ["B3"], ["b_main", "b_peripheral_top", "b_headrest_top", "b_overhead_top"], ["三分支并行", "mapped FIR + delayed mapped PoolIIR", "contribution add", "OutputMapping sum", "output delay"], parent="B.RUNTIME", inputs=[port("peripheral", "audio22", "peripheral:in"), port("headrest", "audio10", "headrest:in"), port("overhead", "audio20", "overhead:in")], outputs=[port("peripheral_out", "audio10", "peripheral:out"), port("headrest_out", "audio40", "headrest:out"), port("overhead_out", "audio4", "overhead:out")], children=["B.HEQ.PERIPHERAL", "B.HEQ.HEADREST", "B.HEQ.OVERHEAD"], edges=[])
    add("B.HEQ.PERIPHERAL", "Peripheral 22→10 HEQ", "42 个 530-tap FIR/IIR contribution 按十组求和并延迟 34 样本", "runtime_b", ["B3"], ["b_main", "b_peripheral_top"], ["22→42 mapping", "FIR+530 延迟 IIR", "10 contiguous sums", "34-sample output ring"], parent="B.HEQ", inputs=[port("in", "audio22")], outputs=[port("out", "audio10")], states=[state("fir", "12342 state"), state("input_delay", "15004 state"), state("output_ring", "340 state")], status="unsupported")
    add("B.HEQ.HEADREST", "Headrest 10→40 HEQ", "同一十路面向四组目标做 40 条长 FIR 与并行 IIR", "runtime_b", ["B3"], ["b_main", "b_headrest_top"], ["10 路重复四次", "40×530 FIR", "530 延迟 PoolIIR", "逐 contribution 相加", "2-sample carry"], parent="B.HEQ", inputs=[port("in", "audio10")], outputs=[port("out", "audio40")], states=[state("fir", "5610 state"), state("iir_delay", "6820 state"), state("carry", "80 state")], status="approximation", component="external_model.heq.headrest")
    add("B.HEQ.OVERHEAD", "Overhead 20→4 HEQ", "二十条 FIR/IIR contribution 每五条求和为四路并使用 34 样本输出环", "runtime_b", ["B3"], ["b_main", "b_overhead_top"], ["20×530 FIR", "530 延迟 PoolIIR", "四组五路求和", "34-sample output ring"], parent="B.HEQ", inputs=[port("in", "audio20")], outputs=[port("out", "audio4")], states=[state("fir", "11220 state"), state("iir_delay", "13640 state"), state("output_ring", "136 state")], status="approximation", component="external_model.heq.overhead", blockers=[blocker("state", "34 样本 delay_line 已按生成环修正，仍需验证先读后写跨块语义", "b_main"), blocker("golden", "并行 IIR/求和/时序 golden 未完成", "b_main")])
    add("B.POST", "降速后空间/VLS", "对 54 路做 SleepingBeauty，并以两行 VLS/串扰消除形成 22 路返回", "runtime_b", ["B1", "B3"], ["b_main", "b_vls", "b_post_top"], ["54 路 SB", "Row1/Row2 generation", "16 路串扰 IIR/delay", "13/3 return SRC"], parent="B.RUNTIME", inputs=[port("peripheral", "audio10", "sleeping:peripheral"), port("headrest", "audio40", "sleeping:headrest"), port("overhead", "audio4", "sleeping:overhead"), port("row1_targets", "gains40", "row1:targets"), port("row2_targets", "gains40", "row2:targets")], outputs=[port("out", "audio22", "return:out")], children=["B.POST.SLEEPING", "B.POST.ROW1", "B.POST.ROW2", "B.POST.RETURN"], edges=[edge("sleeping:peripheral_out", "return:peripheral", "audio10"), edge("sleeping:overhead_out", "return:overhead", "audio4"), edge("sleeping:row1", "row1:in", "audio20"), edge("sleeping:row2", "row2:in", "audio20"), edge("row1:out", "return:row1", "audio4"), edge("row2:out", "return:row2", "audio4")])
    add("B.POST.SLEEPING", "54 路四组 SleepingBeauty", "按 left/right/center/mono 四组表插值与 ramp 处理 peripheral/overhead/headrest", "runtime_b", ["B3"], ["b_main", "b_post_top"], ["54→4 map", "30 点表", "极端策略", "frame-quantized multiplicative ramps"], parent="B.POST", inputs=[port("peripheral", "audio10"), port("headrest", "audio40"), port("overhead", "audio4")], outputs=[port("peripheral_out", "audio10"), port("overhead_out", "audio4"), port("row1", "audio20"), port("row2", "audio20")], states=[state("rampers", "四组 current/target/frame/freeze", [4])], status="unsupported")
    add("B.POST.ROW1", "VLS Row1", "以第一组 20 路 headrest 生成 Drv/Pax 四路并做串扰消除", "runtime_b", ["B3"], ["b_vls", "b_post_top"], ["Drv/Pax generation", "16 path xtalk"], parent="B.POST", inputs=[port("in", "audio20", "generation:in"), port("targets", "gains40", "generation:targets")], outputs=[port("out", "audio4", "xtalk:out")], children=["B.POST.ROW1.GENERATION", "B.POST.ROW1.XTALK"], edges=[edge("generation:out", "xtalk:in", "audio4")])
    add("B.POST.ROW1.GENERATION", "Row1 Drv/Pax FIR 生成", "20 路 sequence-gated gain 后构造 72 个 288-tap FIR contribution 并求四路", "runtime_b", ["B3"], ["b_vls", "b_post_top"], ["20 gain rampers", "Drv36+Pax36 mapped FIR", "四个 18-contribution sums"], parent="B.POST.ROW1", inputs=[port("in", "audio20"), port("targets", "gains40")], outputs=[port("out", "audio4")], states=[state("fir", "Drv/Pax 各 6380 state"), state("gains", "每支 20 active/target")], status="unsupported")
    add("B.POST.ROW1.XTALK", "Row1 串扰消除", "四路重复成 16 path，经独立 IIR/delay 后按目标求和", "runtime_b", ["B3"], ["b_vls", "b_post_top"], ["4→16 repeat", "16 PoolIIR", "16 delay", "4×4 path sum"], parent="B.POST.ROW1", inputs=[port("in", "audio4")], outputs=[port("out", "audio4")], states=[state("iir", "480 state"), state("delay", "600 state")], status="unsupported")
    add("B.POST.ROW2", "VLS Row2", "以第二组 20 路 headrest 生成 Drv/Pax 四路并做串扰消除", "runtime_b", ["B3"], ["b_vls", "b_post_top"], ["Drv/Pax generation", "16 path xtalk"], parent="B.POST", inputs=[port("in", "audio20", "generation:in"), port("targets", "gains40", "generation:targets")], outputs=[port("out", "audio4", "xtalk:out")], children=["B.POST.ROW2.GENERATION", "B.POST.ROW2.XTALK"], edges=[edge("generation:out", "xtalk:in", "audio4")])
    add("B.POST.ROW2.GENERATION", "Row2 Drv/Pax FIR 生成", "20 路 sequence-gated gain 后构造 72 个 288-tap FIR contribution 并求四路", "runtime_b", ["B3"], ["b_vls", "b_post_top"], ["20 gain rampers", "Drv36+Pax36 mapped FIR", "四个 18-contribution sums"], parent="B.POST.ROW2", inputs=[port("in", "audio20"), port("targets", "gains40")], outputs=[port("out", "audio4")], states=[state("fir", "Drv/Pax 各 6380 state"), state("gains", "每支 20 active/target")], status="unsupported")
    add("B.POST.ROW2.XTALK", "Row2 串扰消除", "四路重复成 16 path，经独立 IIR/delay 后按目标求和", "runtime_b", ["B3"], ["b_vls", "b_post_top"], ["4→16 repeat", "16 PoolIIR", "16 delay", "4×4 path sum"], parent="B.POST.ROW2", inputs=[port("in", "audio4")], outputs=[port("out", "audio4")], states=[state("iir", "480 state"), state("delay", "600 state")], status="unsupported")
    add("B.POST.RETURN", "22 路 13/3 返回 SRC", "组合 SB peripheral10/overhead4 与 Row1/2 各四路，再变换回 A 的 1500Hz 块率", "runtime_b", ["B1", "B3"], ["b_main"], ["22 路顺序拼接", "B3 polyphase write", "B1 13/3 SRC read", "发布 22×32"], parent="B.POST", inputs=[port("peripheral", "audio10"), port("overhead", "audio4"), port("row1", "audio4"), port("row2", "audio4")], outputs=[port("out", "audio22")], states=[state("src_buffer", "2112 polyphase state")], status="unsupported")


def build_document() -> dict[str, Any]:
    if not DEFINITIONS:
        build_definitions()
    types = []
    def t(type_id: str, kind: str, dtype: str, shape: list[int], rate: float | None = None, block: int | None = None):
        item: dict[str, Any] = {"id": type_id, "kind": kind, "dtype": dtype, "shape": shape}
        if rate:
            item["sample_rate_hz"] = rate
        if block:
            item["block_size"] = block
        types.append(item)
    for channels in [1, 2, 4, 6, 7, 8, 10, 12, 13, 17, 20, 21, 22, 25, 30, 32, 40, 52, 54]:
        t(f"audio{channels}", "audio", "f32", [channels, 32], 48000, 32)
    for channels in [2, 6, 8]:
        t(f"audio{channels}_128", "audio", "f32", [channels, 128], 48000, 128)
    t("audio10_2", "audio", "f32", [10, 2], 3000, 2)
    for channels in [2, 4, 6, 8]:
        t(f"spectrum{channels}", "spectrum", "complex_f32", [channels, 129])
    for count in [1, 2, 4, 10]:
        t(f"control{count}", "control", "f32", [count])
    for count in [40, 80, 166, 1134]:
        t(f"gains{count}", "bulk", "f32", [count])
    t("event1", "event", "u32", [1])
    t("bus488", "message", "struct", [488])
    t("bus1193", "message", "struct", [1193])
    sources = [{"id": sid, "relative_file": path, "sha256": digest} for sid, path, digest in SOURCES]
    parameter_partitions = [
        {"id": "A.InputSelect.p0", "source_refs": ["a_input_select_top"], "field_count": 4, "owner_refs": ["A.INPUT_SELECT"], "field_groups": ["30 路 router", "12/1/17 selectors"], "extraction_status": "extracted"},
        {"id": "A.PreAmp.p0", "source_refs": ["a_preamp_top"], "field_count": 54, "owner_refs": ["A.PREAMP.INPUT_MIXER", "A.PREAMP.MUSIC_VOLUME", "A.PREAMP.BASS", "A.PREAMP.TREBLE", "A.PREAMP.MID", "A.PREAMP.BALANCE", "A.PREAMP.ROUTE_LEVEL"], "field_groups": ["InputMixer", "Volume", "Bass/Treble/Mid", "Balance", "PreqRouter", "LevelDetect"], "extraction_status": "extracted"},
        {"id": "A.FDP.p3", "source_refs": ["a_fdp_top"], "field_count": 12, "owner_refs": ["A.SPATIAL_FULL.FDP"], "field_groups": ["ESD bounds/step", "SPUM fast/slow/overwrite/direct delay", "TrebleDelay"], "extraction_status": "indexed"},
        {"id": "A.Mix.p5", "source_refs": ["a_mix_top"], "field_count": 4, "owner_refs": ["A.SPATIAL_FULL.MIX"], "field_groups": ["MixEQ coefficients/stages", "ramp coefficient", "surround delay"], "extraction_status": "indexed"},
        {"id": "A.Peripheral.p8", "source_refs": ["a_peripheral_top"], "field_count": 2, "owner_refs": ["A.SPATIAL_FULL.PERIPHERAL_EQ"], "field_groups": ["22 路 IIR coefficients/stages"], "extraction_status": "indexed"},
        {"id": "A.Holigram.p12", "source_refs": ["a_holigram_top"], "field_count": 7, "owner_refs": ["A.SPATIAL_FULL.POST_HOLOGRAM"], "field_groups": ["SleepingBeauty map/table/policy", "phase alignment delays"], "extraction_status": "indexed"},
        {"id": "A.Fade.p13", "source_refs": ["a_fade_top"], "field_count": 12, "owner_refs": ["A.SPATIAL_FULL.FADE_SUM"], "field_groups": ["Fade table/map/policy", "LPF coefficients", "mute", "output route", "speaker delays"], "extraction_status": "indexed"},
        {"id": "A.Audiopilot.p14", "source_refs": ["a_audiopilot_top"], "field_count": 101, "owner_refs": ["A.AUDIOPILOT"], "field_groups": ["HF filters/estimator/slew", "LF filters/estimator/speed", "ratio", "BoostMap", "gain application"], "extraction_status": "indexed"},
        {"id": "A.PostProcess.p0", "source_refs": ["a_post_top"], "field_count": 34, "owner_refs": ["A.POSTPROCESS"], "field_groups": ["ASD router", "limiter high/low", "PostEQ/FreqComp", "two soft clippers", "mute", "test matrix", "delay"], "extraction_status": "extracted"},
        {"id": "B.Bands.p1", "source_refs": ["b_bands_top"], "field_count": 34, "owner_refs": ["B.BANDS"], "field_groups": ["SRC", "mono detector", "decay", "bass/mid filters"], "extraction_status": "indexed"},
        {"id": "B.FDP.p2", "source_refs": ["b_fdp_top"], "field_count": 11, "owner_refs": ["B.FDP"], "field_groups": ["stretching metadata", "reverb/impulse parameters"], "extraction_status": "indexed"},
        {"id": "B.Mix.p4", "source_refs": ["b_mix_top"], "field_count": 10, "owner_refs": ["B.MIX_DECI"], "field_groups": ["MixEQ", "wiggle FIR", "delays", "ramp pole"], "extraction_status": "indexed"},
        {"id": "B.Control.p6", "source_refs": ["b_control_top"], "field_count": 73, "owner_refs": ["B.MIX_CONTROL"], "field_groups": ["seven target families", "alternate arrays", "sequence delays"], "extraction_status": "indexed"},
        {"id": "B.Peripheral.p7", "source_refs": ["b_peripheral_top", "b_peripheral_fir"], "field_count": 8, "owner_refs": ["B.HEQ.PERIPHERAL"], "field_groups": ["42 mappings/lengths/delays/stages", "5880 IIR", "22260 FIR", "10 sum starts"], "extraction_status": "indexed"},
        {"id": "B.Overhead.p9", "source_refs": ["b_overhead_top", "b_overhead_fir"], "field_count": 8, "owner_refs": ["B.HEQ.OVERHEAD"], "field_groups": ["20 mappings/lengths/delays/stages", "2800 IIR", "10600 FIR", "4 sum starts"], "extraction_status": "extracted"},
        {"id": "B.Headrest.p10", "source_refs": ["b_headrest_top", "b_headrest_fir"], "field_count": 8, "owner_refs": ["B.HEQ.HEADREST"], "field_groups": ["40 mappings/lengths/delays/stages", "5600 IIR", "21200 FIR", "40 sum starts"], "extraction_status": "extracted"},
        {"id": "B.Post.p11", "source_refs": ["b_post_top", "b_vls_row1_fir", "b_vls_row2_fir"], "field_count": 22, "owner_refs": ["B.POST", "B.POST.SLEEPING", "B.POST.ROW1.GENERATION", "B.POST.ROW1.XTALK", "B.POST.ROW2.GENERATION", "B.POST.ROW2.XTALK", "B.POST.RETURN"], "field_groups": ["54 SB map/table", "VLS length/map/ramp", "two 976-IIR arrays", "two delay arrays", "four 10368 FIR banks"], "extraction_status": "indexed"},
    ]
    definitions = copy.deepcopy(DEFINITIONS)
    definitions_by_id = {item["id"]: item for item in definitions}
    for partition in parameter_partitions:
        for owner in partition["owner_refs"]:
            definitions_by_id[owner]["parameter_refs"].append(partition["id"])
    tasks = [
        {"id": "A0", "runtime_ref": "runtime_a", "source_tid": 0, "period_ticks": 1, "phase_ticks": 0, "ordinal_calls": ["A.SPATIAL_FULL.FDP", "A.SPATIAL_FULL.MIX", "A.SPATIAL_FULL.PERIPHERAL_EQ", "A.SPATIAL_FULL.POST_HOLOGRAM", "A.SPATIAL_FULL.FADE_SUM", "A.INPUT_SELECT", "A.PREAMP", "A.POSTPROCESS", "A.AUDIOPILOT"]},
        {"id": "A1", "runtime_ref": "runtime_a", "source_tid": 1, "period_ticks": 2, "phase_ticks": {"status": "unknown", "reason": "host coincident-call phase unverified"}, "ordinal_calls": ["A.AUDIOPILOT.LF"]},
        {"id": "A2", "runtime_ref": "runtime_a", "source_tid": 2, "period_ticks": 4, "phase_ticks": {"status": "unknown", "reason": "host coincident-call phase unverified"}, "ordinal_calls": ["A.SPATIAL_FULL.FDP"]},
        {"id": "A3", "runtime_ref": "runtime_a", "source_tid": 3, "period_ticks": 64, "phase_ticks": {"status": "unknown", "reason": "host coincident-call phase unverified"}, "ordinal_calls": ["A.AUDIOPILOT.HF_ESTIMATOR"]},
        {"id": "A4", "runtime_ref": "runtime_a", "source_tid": 4, "period_ticks": 256, "phase_ticks": {"status": "unknown", "reason": "host coincident-call phase unverified"}, "ordinal_calls": ["A.AUDIOPILOT.LF"]},
        {"id": "A5", "runtime_ref": "runtime_a", "source_tid": 5, "period_ticks": 768, "phase_ticks": {"status": "unknown", "reason": "host coincident-call phase unverified"}, "ordinal_calls": ["A.AUDIOPILOT.LF"]},
        {"id": "B0", "runtime_ref": "runtime_b", "source_tid": 0, "period_ticks": 1, "phase_ticks": 0, "ordinal_calls": []},
        {"id": "B1", "runtime_ref": "runtime_b", "source_tid": 1, "period_ticks": 3, "phase_ticks": {"status": "unknown", "reason": "host coincident-call order unverified"}, "ordinal_calls": ["B.BANDS", "B.MIX_CONTROL", "B.POST.RETURN"]},
        {"id": "B2", "runtime_ref": "runtime_b", "source_tid": 2, "period_ticks": 12, "phase_ticks": {"status": "unknown", "reason": "host coincident-call order unverified"}, "ordinal_calls": ["B.BANDS.DECAY"]},
        {"id": "B3", "runtime_ref": "runtime_b", "source_tid": 3, "period_ticks": 13, "phase_ticks": {"status": "unknown", "reason": "host coincident-call order unverified"}, "ordinal_calls": ["B.BANDS", "B.FDP", "B.MIX_CONTROL", "B.MIX_DECI", "B.HEQ", "B.POST"]},
        {"id": "B4", "runtime_ref": "runtime_b", "source_tid": 4, "period_ticks": 52, "phase_ticks": {"status": "unknown", "reason": "host coincident-call order unverified"}, "ordinal_calls": ["B.FDP"]},
    ]
    # Cross-runtime buses use exact non-overlapping field types.
    extra_types = [
        {"id": "gains26", "kind": "bulk", "dtype": "f32", "shape": [26]},
        {"id": "gains70", "kind": "bulk", "dtype": "f32", "shape": [70]},
    ]
    types.extend(extra_types)
    buses = [
        {"id": "bus_a_to_b", "producer_runtime": "runtime_a", "consumer_runtime": "runtime_b", "total_words": 488, "atomicity": "unverified", "semantic_summary": {"audio": "自适应音频 10ch × 32帧", "control": "当前增益 26/70/70 + Fade 2"}, "fields": [
            {"id": "adaptive_audio", "offset_words": 0, "type_ref": "audio10", "producer_definition": "A.AUDIOPILOT", "consumer_definition": "B.BANDS"},
            {"id": "center_current", "offset_words": 320, "type_ref": "gains26", "producer_definition": "A.SPATIAL_FULL.MIX.RAMP_CONTROL", "consumer_definition": "B.MIX_CONTROL"},
            {"id": "left_current", "offset_words": 346, "type_ref": "gains70", "producer_definition": "A.SPATIAL_FULL.MIX.RAMP_CONTROL", "consumer_definition": "B.MIX_CONTROL"},
            {"id": "right_current", "offset_words": 416, "type_ref": "gains70", "producer_definition": "A.SPATIAL_FULL.MIX.RAMP_CONTROL", "consumer_definition": "B.MIX_CONTROL"},
            {"id": "fade_targets", "offset_words": 486, "type_ref": "control2", "producer_definition": "A.SPATIAL_FULL.FADE_SUM", "consumer_definition": "B.MIX_CONTROL"},
        ]},
        {"id": "bus_b_to_a", "producer_runtime": "runtime_b", "consumer_runtime": "runtime_a", "total_words": 1193, "atomicity": "unverified", "sequence_field": "fast_sequence", "semantic_summary": {"audio": "Mono 1ch + Treble 2/7ch + 空间输出 22ch；每块 32帧", "control": "使能/衰减 + 目标增益 26/70/70 + Sequence"}, "fields": [
            {"id": "enhancement_enable", "offset_words": 0, "type_ref": "event1", "producer_definition": "B.BANDS", "consumer_definition": "A.RUNTIME"},
            {"id": "decay", "offset_words": 1, "type_ref": "control1", "producer_definition": "B.BANDS.DECAY", "consumer_definition": "A.SPATIAL_FULL.FDP.REVERB"},
            {"id": "mono", "offset_words": 2, "type_ref": "audio1", "producer_definition": "B.BANDS.MONO", "consumer_definition": "A.AUDIOPILOT.LF"},
            {"id": "treble_lr", "offset_words": 34, "type_ref": "audio2", "producer_definition": "B.BANDS.SRC", "consumer_definition": "A.SPATIAL_FULL.FDP"},
            {"id": "treble_surround", "offset_words": 98, "type_ref": "audio7", "producer_definition": "B.BANDS.SRC", "consumer_definition": "A.SPATIAL_FULL.MIX"},
            {"id": "center_targets", "offset_words": 322, "type_ref": "gains26", "producer_definition": "B.MIX_CONTROL.TARGETS", "consumer_definition": "A.SPATIAL_FULL.MIX.RAMP_CONTROL"},
            {"id": "left_targets", "offset_words": 348, "type_ref": "gains70", "producer_definition": "B.MIX_CONTROL.TARGETS", "consumer_definition": "A.SPATIAL_FULL.MIX.RAMP_CONTROL"},
            {"id": "right_targets", "offset_words": 418, "type_ref": "gains70", "producer_definition": "B.MIX_CONTROL.TARGETS", "consumer_definition": "A.SPATIAL_FULL.MIX.RAMP_CONTROL"},
            {"id": "post_spatial", "offset_words": 488, "type_ref": "audio22", "producer_definition": "B.POST.RETURN", "consumer_definition": "A.SPATIAL_FULL.FADE_SUM"},
            {"id": "fast_sequence", "offset_words": 1192, "type_ref": "event1", "producer_definition": "B.MIX_CONTROL.SEQUENCE", "consumer_definition": "A.SPATIAL_FULL.MIX.RAMP_CONTROL"},
        ]},
    ]
    relations = []
    unresolved = [
        {"id": "host_phase", "reason": "同 tick 多 TID 的宿主调用相位与抢占关系未验证", "required_check": "读取宿主调度实现并记录调用序", "source_refs": ["a_main", "b_main"]},
        {"id": "bus_atomicity", "reason": "488/1193 words 的物理 swap 原子性与延迟未验证", "required_check": "分析 host transport/ping-pong 实现", "source_refs": ["bus"]},
        {"id": "numeric_goldens", "reason": "所有模块尚未完成参考向量逐块对拍", "required_check": "建立目标参考捕获与模块 golden"},
        {"id": "runtime_downloads", "reason": "TOP/RTC/RMDL 的下载事务、mute/reset/init 协调尚未完整复现", "required_check": "逐 RTC handler 建状态机测试", "source_refs": ["a_main", "b_main"]},
        {"id": "overhead_delay", "reason": "HEQ 切片已改为 34 样本 delay_line，但先读后写环的数值等价尚未对拍", "required_check": "采集 Overhead 跨块 impulse golden", "source_refs": ["b_main"]},
        {"id": "hardware_numeric", "reason": "accelerator FIR/IIR/FFT 的精度、舍入和 reduction order 未验证", "required_check": "目标端与软件实现逐块比对"},
    ]
    stereo_to_system_matrix = ",".join(
        str(value)
        for value in ([1, 0, 0, 1] + [0] * 56)
    )
    return {
        "schema_version": "2.0",
        "model_id": "external_model.dual_runtime_audio",
        "name": "双 Runtime 全量算法索引",
        "sources": sources,
        "types": types,
        "runtimes": [
            {"id": "runtime_a", "clock_hz": {"num": 1500, "den": 1}, "notes": ["48kHz/32 sample base audio block"]},
            {"id": "runtime_b", "clock_hz": {"num": 4500, "den": 1}, "notes": ["B3 period 13 is not an integer divisor of Runtime A block rate"]},
        ],
        "tasks": tasks,
        "buses": buses,
        "parameter_partitions": parameter_partitions,
        "definitions": definitions,
        "relations": relations,
        "system": {
            "roots": ["A.RUNTIME", "B.RUNTIME"],
            "io_projection": [
                {
                    "id": "program_input",
                    "direction": "input",
                    "type_ref": "audio2",
                    "component": "orpheus.builtin.embed_in",
                    "params": {"channels": 2, "sample_rate": 48000},
                    "endpoint": "A.RUNTIME:audio_in",
                    "adapter": {
                        "id": "program_to_system",
                        "component": "orpheus.builtin.output_router",
                        "params": {
                            "channels_in": 2,
                            "channels_out": 30,
                            "matrix": stereo_to_system_matrix,
                        },
                        "output_type_ref": "audio30",
                    },
                },
                {
                    "id": "main_output",
                    "direction": "output",
                    "type_ref": "audio22",
                    "component": "orpheus.builtin.embed_out",
                    "params": {"channels": 22, "sample_rate": 48000},
                    "endpoint": "A.RUNTIME:main_out",
                },
            ],
            "notes": ["完整研究图允许 placeholder；可执行发布必须消除全部 blocker"],
        },
        "coverage": {"definition_ids": [item["id"] for item in definitions], "unresolved": unresolved, "exclusions": []},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="构建双 Runtime 全量 Algorithm IR")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    document = build_document()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        yaml.safe_dump(document, sort_keys=False, allow_unicode=True, width=120),
        encoding="utf-8",
    )
    print(f"wrote {len(document['definitions'])} definitions to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())