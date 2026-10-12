#!/usr/bin/env python3
"""Extract external Model_1_2 HEQ defaults and emit decomposed component graphs."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_external_top import extract_float_array  # noqa: E402


SPECS = {
    "headrest": {
        "metadata_file": "Model_1_2_PreAmp_p10_b0_TOP.c",
        "coefficient_file": "Model_1_2_PreAmp_p10_b1_TOP.c",
        "field_root": "HeadrestCompEq",
        "input_channels": 10,
        "filter_count": 40,
        "output_count": 40,
        "coefficient_count": 21200,
        "iir_coefficient_count": 5600,
    },
    "overhead": {
        "metadata_file": "Model_1_2_PreAmp_p9_b0_TOP.c",
        "coefficient_file": "Model_1_2_PreAmp_p9_b1_TOP.c",
        "field_root": "OverheadHeq",
        "input_channels": 20,
        "filter_count": 20,
        "output_count": 4,
        "coefficient_count": 10600,
        "iir_coefficient_count": 2800,
    },
}


def field_ending_with(path: Path, suffix: str) -> str:
    source = path.read_text(encoding="utf-8")
    fields = re.findall(r"/\*\s*([A-Za-z0-9_]+)\s*\*/", source)
    matches = [field for field in fields if field.endswith(suffix)]
    if len(matches) != 1:
        raise ValueError(f"{path.name}: expected one field ending with {suffix!r}, got {matches}")
    return matches[0]


def extract_suffix(path: Path, suffix: str, count: int) -> list[float]:
    field = field_ending_with(path, suffix)
    values = extract_float_array(path.read_text(encoding="utf-8"), field)
    if len(values) != count:
        raise ValueError(f"{field}: expected {count} values, got {len(values)}")
    return values


def integers(values: list[float], field: str) -> list[int]:
    result = [int(value) for value in values]
    if any(float(integer) != value for integer, value in zip(result, values)):
        raise ValueError(f"{field}: contains non-integer values")
    return result


def add_sum_tree(nodes: list[dict], connections: list[dict], sources: list[str]) -> str:
    level = 0
    queue = list(sources)
    while len(queue) > 1:
        next_queue: list[str] = []
        for pair in range(0, len(queue), 2):
            if pair + 1 >= len(queue):
                next_queue.append(queue[pair])
                continue
            mixer_id = f"sum_l{level}_{pair // 2}"
            nodes.append({
                "id": mixer_id,
                "component": "orpheus.builtin.mixer",
                "params": {"channels": 1, "gain0": 0.0, "gain1": 0.0},
                "position": {
                    "x": 540 + level * 220,
                    "y": 80 + (pair // 2) * 110,
                },
            })
            connections.extend([
                {"from": queue[pair], "to": f"{mixer_id}:in0"},
                {"from": queue[pair + 1], "to": f"{mixer_id}:in1"},
            ])
            next_queue.append(f"{mixer_id}:out")
        queue = next_queue
        level += 1
    return queue[0]


def resource_definitions(name: str, spec: dict, metadata: dict) -> dict:
    return {
        f"external_model.heq.{name}_fir": {
            "file": f"assets/{name}_fir.f32",
            "format": "f32le",
            "shape": [spec["filter_count"], 530],
            "sha256": metadata["coefficient_sha256"],
        },
        f"external_model.heq.{name}_iir": {
            "file": f"assets/{name}_iir.f32",
            "format": "f32le",
            "shape": [spec["filter_count"], 140],
            "sha256": metadata["iir_coefficient_sha256"],
        },
    }


def coefficient_offsets(metadata: dict) -> list[int]:
    offsets: list[int] = []
    cursor = 0
    for length in metadata["filter_lengths"]:
        offsets.append(cursor)
        cursor += length
    return offsets


def fir_node(name: str, index: int, metadata: dict, x: int, y: int) -> dict:
    coefficient_set = metadata["coefficient_mapping"][index]
    offsets = coefficient_offsets(metadata)
    return {
        "id": f"fir_{index:02d}",
        "component": "orpheus.builtin.fir",
        "params": {
            "channels": 1,
            "coefficients": {
                "$resource": f"external_model.heq.{name}_fir",
                "offset": offsets[coefficient_set],
                "count": metadata["filter_lengths"][coefficient_set],
            },
        },
        "position": {"x": x, "y": y},
    }


def group_document(name: str, group: int, spec: dict, metadata: dict) -> dict:
    if name == "headrest":
        indices = list(range(group * 10, group * 10 + 10))
        nodes = [
            fir_node(name, index, metadata, 260, 40 + local * 100)
            for local, index in enumerate(indices)
        ]
        ports = [
            {
                "id": f"in{local}",
                "direction": "input",
                "type": "audio",
                "sample_format": "f32",
                "channels": 1,
                "maps_to": f"fir_{index:02d}:in",
            }
            for local, index in enumerate(indices)
        ]
        ports.extend(
            {
                "id": f"out{local}",
                "direction": "output",
                "type": "audio",
                "sample_format": "f32",
                "channels": 1,
                "maps_to": f"fir_{index:02d}:out",
            }
            for local, index in enumerate(indices)
        )
        connections: list[dict] = []
        display_name = f"头枕补偿输出组 {group + 1}"
        description = (
            f"让同一组 10 路源信号在送往输出 {indices[0]}..{indices[-1]} 前"
            "分别使用独立补偿曲线，便于按目标输出调音"
        )
        imports = [{"component": "orpheus.builtin.fir"}]
    else:
        start = metadata["output_starts"][group]
        end = metadata["output_starts"][group + 1] if group < 3 else spec["filter_count"]
        indices = list(range(start, end))
        nodes = [
            fir_node(name, index, metadata, 260, 40 + local * 100)
            for local, index in enumerate(indices)
        ]
        connections = []
        output = add_sum_tree(
            nodes,
            connections,
            [f"fir_{index:02d}:out" for index in indices],
        )
        ports = [
            {
                "id": f"in{local}",
                "direction": "input",
                "type": "audio",
                "sample_format": "f32",
                "channels": 1,
                "maps_to": f"fir_{index:02d}:in",
            }
            for local, index in enumerate(indices)
        ]
        ports.append({
            "id": "out",
            "direction": "output",
            "type": "audio",
            "sample_format": "f32",
            "channels": 1,
            "maps_to": output,
        })
        display_name = f"顶置输出合成组 {group + 1}"
        mapped_inputs = [metadata["input_mapping"][index] for index in indices]
        description = (
            f"先分别修正输入通道 {mapped_inputs}，再合成为一路输出，"
            "避免用一条公共 EQ 覆盖各支路差异"
        )
        imports = [
            {"component": "orpheus.builtin.fir"},
            {"component": "orpheus.builtin.mixer"},
        ]

    return {
        "id": f"external_model.heq.{name}.group_{group}",
        "name": display_name,
        "category": "外部模型/均衡器",
        "description": description,
        "version": "7.736.0",
        "abi_version": 1,
        "package_type": "composite",
        "imports": imports,
        "ports": ports,
        "parameters": [],
        "resources": resource_definitions(name, spec, metadata),
        "graph": {"nodes": nodes, "connections": connections},
        "execution": {"realtime_safe": True},
    }


def composite_document(name: str, spec: dict, metadata: dict) -> dict:
    input_channels = spec["input_channels"]
    output_count = spec["output_count"]
    output_delay = 2 if name == "headrest" else 34
    nodes: list[dict] = [{
        "id": "split",
        "component": "orpheus.builtin.deinterleave",
        "params": {"channels": input_channels},
        "position": {"x": 40, "y": 260},
    }]
    connections: list[dict] = []
    for group in range(4):
        group_id = f"group_{group}"
        nodes.append({
            "id": group_id,
            "component": f"external_model.heq.{name}.group_{group}",
            "position": {"x": 300, "y": 40 + group * 160},
        })
        if name == "headrest":
            for local in range(10):
                index = group * 10 + local
                connections.extend([
                    {
                        "from": f"split:out{metadata['input_mapping'][index]}",
                        "to": f"{group_id}:in{local}",
                    },
                    {"from": f"{group_id}:out{local}", "to": f"join:in{index}"},
                ])
        else:
            start = metadata["output_starts"][group]
            end = metadata["output_starts"][group + 1] if group < 3 else spec["filter_count"]
            for local, index in enumerate(range(start, end)):
                connections.append({
                    "from": f"split:out{metadata['input_mapping'][index]}",
                    "to": f"{group_id}:in{local}",
                })
            connections.append({"from": f"{group_id}:out", "to": f"join:in{group}"})

    nodes.extend([
        {
            "id": "join",
            "component": "orpheus.builtin.interleave",
            "params": {"channels": output_count},
            "position": {"x": 620, "y": 260},
        },
        {
            "id": "carry",
            "component": "orpheus.builtin.delay_line",
            "params": {
                "channels": output_count,
                "max_delay_samples": output_delay,
                "delays_samples": ",".join([str(output_delay)] * output_count),
            },
            "position": {"x": 860, "y": 260},
        },
    ])
    connections.append({"from": "join:out", "to": "carry:in"})

    display_name = "头枕补偿均衡" if name == "headrest" else "顶置声道合成均衡"
    description = (
        "让 10 路源信号面向四组目标输出分别调音，避免不同目标共用同一组补偿系数"
        if name == "headrest"
        else "在合成四路顶置输出前逐支路补偿，使每个输入对目标声道的贡献可独立校正"
    )
    return {
        "id": f"external_model.heq.{name}",
        "name": display_name,
        "category": "外部模型/均衡器",
        "description": description,
        "version": "7.736.0",
        "abi_version": 1,
        "package_type": "composite",
        "imports": [
            *[f"{name}_group_{group}.component.yaml" for group in range(4)],
            {"component": "orpheus.builtin.deinterleave"},
            {"component": "orpheus.builtin.interleave"},
            {"component": "orpheus.builtin.delay_line"},
        ],
        "ports": [
            {"id": "in", "direction": "input", "type": "audio", "sample_format": "f32", "channels": input_channels, "maps_to": "split:in"},
            {"id": "out", "direction": "output", "type": "audio", "sample_format": "f32", "channels": output_count, "maps_to": "carry:out"},
        ],
        "parameters": [],
        "graph": {"nodes": nodes, "connections": connections},
        "execution": {"latency_samples": 529 + output_delay, "realtime_safe": True},
    }


def write_node_notes(project_root: Path, documents: dict[str, dict], metadata: dict[str, dict]) -> None:
    notes = {
        "headrest_input": "提供可重复的十路测试信号，用来检查四组补偿输出是否保持正确的通道对应关系。",
        "headrest": "为同一组十路源信号准备四套可独立调音的输出路径，便于不同目标输出使用不同补偿曲线。",
        "overhead_input": "提供可重复的二十路测试信号，用来检查每个顶置输出只汇聚约定的五条支路。",
        "overhead": "先逐支路修正频响再合成四路输出，避免合成后只能用一条公共 EQ 修正所有来源。",
        "overhead_sink": "吸收四路结果，使示例可以完整编译运行并检查顶置合成链路。",
    }
    for component_id, document in documents.items():
        component_id = document["id"]
        name = "headrest" if ".headrest" in component_id else "overhead"
        input_mapping = metadata[name]["input_mapping"]
        for node in document["graph"]["nodes"]:
            key = f"sub:{component_id}/{node['id']}"
            if node["id"] == "split":
                notes[key] = "把总线通道暴露给各条补偿路径，便于同一源通道按 InputMapping 扇出到不同目标。"
            elif node["id"].startswith("group_"):
                group = int(node["id"].split("_")[1])
                if name == "headrest":
                    notes[key] = (
                        f"让输入 0..9 在送往输出 {group * 10}..{group * 10 + 9} 前分别调音；"
                        "单独成组是为了保留这一目标组的独立系数更新边界。"
                    )
                else:
                    start = metadata[name]["output_starts"][group]
                    end = metadata[name]["output_starts"][group + 1] if group < 3 else 20
                    mapped = input_mapping[start:end]
                    notes[key] = (
                        f"把输入通道 {mapped} 对该目标输出的贡献分别校正后相加；"
                        "先滤波再求和可保留各支路不同的补偿需求。"
                    )
            elif node["id"].startswith("fir_"):
                index = int(node["id"].split("_")[1])
                reference = node["params"]["coefficients"]
                notes[key] = (
                    f"在源输入通道 {input_mapping[index]} 送往输出支路 {index} 前做独立频响与相位补偿；"
                    f"使用原模型的 530-tap 系数切片（offset={reference['offset']}）。"
                )
            elif node["id"].startswith("sum_"):
                notes[key] = "逐级汇聚已补偿支路；使用平衡二叉树是为了复用通用二输入 mixer，同时避免一条过长的串行求和链。"
            elif node["id"] == "join":
                notes[key] = "按下游约定的 OutputMapping 顺序打包结果，避免补偿正确但输出通道错位。"
            elif node["id"] == "carry":
                delay = 2 if name == "headrest" else 34
                notes[key] = (
                    f"复现生成实现的 {delay} 样本输出时序状态；"
                    "必须保留先读后写和跨块环语义，不能按画布整洁度缩短。"
                )
    (project_root / "node-notes.json").write_text(
        json.dumps(notes, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def extract_one(source_root: Path, output_dir: Path, name: str, spec: dict) -> dict:
    metadata_path = source_root / spec["metadata_file"]
    coefficient_path = source_root / spec["coefficient_file"]
    root = spec["field_root"]
    filter_count = spec["filter_count"]
    output_count = spec["output_count"]
    fields = {
        "filter_lengths": integers(extract_suffix(metadata_path, f"{root}FirCoeffsLengths", filter_count), "filter_lengths"),
        "coefficient_mapping": integers(extract_suffix(metadata_path, f"{root}FirCoeffsMapping", filter_count), "coefficient_mapping"),
        "input_mapping": integers(extract_suffix(metadata_path, f"{root}FirInputMapping", filter_count), "input_mapping"),
        "fir_delays": integers(extract_suffix(metadata_path, f"{root}FirDelays", filter_count), "fir_delays"),
        "output_starts": integers(extract_suffix(metadata_path, f"{root}OutputMapping", output_count), "output_starts"),
        "iir_num_stages": integers(extract_suffix(metadata_path, f"{root}PoolIirPooliirNumStages", filter_count), "iir_num_stages"),
    }
    coefficients = extract_suffix(coefficient_path, f"{root}FirCoeffsTarget", spec["coefficient_count"])
    iir_coefficients = extract_suffix(metadata_path, f"{root}PoolIirpooliirCoeffs", spec["iir_coefficient_count"])
    if sum(fields["filter_lengths"]) != len(coefficients):
        raise ValueError(f"{name}: filter lengths do not sum to coefficient count")
    raw = struct.pack(f"<{len(coefficients)}f", *coefficients)
    iir_raw = struct.pack(f"<{len(iir_coefficients)}f", *iir_coefficients)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / f"{name}_fir.f32").write_bytes(raw)
    (output_dir / f"{name}_iir.f32").write_bytes(iir_raw)
    metadata = {
        "source": [spec["metadata_file"], spec["coefficient_file"]],
        "source_model_version": "7.736",
        "coefficient_count": len(coefficients),
        "coefficient_sha256": hashlib.sha256(raw).hexdigest(),
        "iir_coefficient_count": len(iir_coefficients),
        "iir_coefficient_sha256": hashlib.sha256(iir_raw).hexdigest(),
        **fields,
    }
    (output_dir / f"{name}_metadata.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return metadata


def write_components(output_dir: Path, name: str, spec: dict, metadata: dict) -> dict[str, dict]:
    documents = {
        f"external_model.heq.{name}": composite_document(name, spec, metadata),
        **{
            f"external_model.heq.{name}.group_{group}": group_document(
                name, group, spec, metadata
            )
            for group in range(4)
        },
    }
    component_root = output_dir.parent
    (component_root / f"{name}.component.yaml").write_text(
        yaml.safe_dump(documents[f"external_model.heq.{name}"], sort_keys=False, allow_unicode=True),
        encoding="utf-8",
    )
    for group in range(4):
        component_id = f"external_model.heq.{name}.group_{group}"
        (component_root / f"{name}_group_{group}.component.yaml").write_text(
            yaml.safe_dump(documents[component_id], sort_keys=False, allow_unicode=True),
            encoding="utf-8",
        )
    return documents


def main() -> int:
    parser = argparse.ArgumentParser(description="提取外部 Model_1_2 HEQ 默认资源并生成基础组件组合图")
    parser.add_argument("source_root", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    summary = {}
    documents = {}
    for name, spec in SPECS.items():
        summary[name] = extract_one(args.source_root, args.output_dir, name, spec)
        documents.update(write_components(args.output_dir, name, spec, summary[name]))
    write_node_notes(args.output_dir.parents[2], documents, summary)
    print(json.dumps(summary, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())