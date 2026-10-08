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


def add_sum_tree(
    nodes: list[dict], connections: list[dict], sources: list[str], group: int
) -> str:
    level = 0
    queue = list(sources)
    while len(queue) > 1:
        next_queue: list[str] = []
        for pair in range(0, len(queue), 2):
            if pair + 1 >= len(queue):
                next_queue.append(queue[pair])
                continue
            mixer_id = f"sum_g{group:02d}_l{level}_{pair // 2}"
            nodes.append({
                "id": mixer_id,
                "component": "orpheus.builtin.mixer",
                "params": {"channels": 1, "gain0": 0.0, "gain1": 0.0},
                "position": {
                    "x": 540 + level * 220,
                    "y": 80 + group * 430 + (pair // 2) * 110,
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


def composite_document(name: str, spec: dict, metadata: dict) -> dict:
    input_channels = spec["input_channels"]
    filter_count = spec["filter_count"]
    output_count = spec["output_count"]
    nodes: list[dict] = [{
        "id": "split",
        "component": "orpheus.builtin.deinterleave",
        "params": {"channels": input_channels},
        "position": {"x": 40, "y": 450 if name == "headrest" else 800},
    }]
    connections: list[dict] = []
    coefficient_offsets: list[int] = []
    cursor = 0
    for length in metadata["filter_lengths"]:
        coefficient_offsets.append(cursor)
        cursor += length
    filter_outputs: list[str] = []
    for index in range(filter_count):
        filter_id = f"fir_{index:02d}"
        coefficient_set = metadata["coefficient_mapping"][index]
        nodes.append({
            "id": filter_id,
            "component": "orpheus.builtin.fir",
            "params": {
                "channels": 1,
                "coefficients": {
                    "$resource": f"external_model.heq.{name}_fir",
                    "offset": coefficient_offsets[coefficient_set],
                    "count": metadata["filter_lengths"][coefficient_set],
                },
            },
            "position": (
                {"x": 260 + (index // 10) * 220, "y": 40 + (index % 10) * 100}
                if name == "headrest"
                else {"x": 260, "y": 40 + index * 90}
            ),
        })
        connections.append({
            "from": f"split:out{metadata['input_mapping'][index]}",
            "to": f"{filter_id}:in",
        })
        filter_outputs.append(f"{filter_id}:out")

    group_outputs: list[str] = []
    for output in range(output_count):
        start = metadata["output_starts"][output]
        end = (
            metadata["output_starts"][output + 1]
            if output + 1 < output_count
            else filter_count
        )
        group_outputs.append(add_sum_tree(nodes, connections, filter_outputs[start:end], output))

    nodes.extend([
        {
            "id": "join",
            "component": "orpheus.builtin.interleave",
            "params": {"channels": output_count},
            "position": {"x": 1200, "y": 450 if name == "headrest" else 700},
        },
        {
            "id": "carry",
            "component": "orpheus.builtin.delay_line",
            "params": {
                "channels": output_count,
                "max_delay_samples": 2,
                "delays_samples": ",".join(["2"] * output_count),
            },
            "position": {"x": 1420, "y": 450 if name == "headrest" else 700},
        },
    ])
    for output, source in enumerate(group_outputs):
        connections.append({"from": source, "to": f"join:in{output}"})
    connections.append({"from": "join:out", "to": "carry:in"})

    return {
        "id": f"external_model.heq.{name}",
        "name": f"HEQ {name.title()} 默认 FIR",
        "category": "外部模型/均衡器",
        "description": (
            f"{input_channels} 路输入、{filter_count} 个 530-tap 基础 FIR、"
            f"{output_count} 路分组输出的可展开组合"
        ),
        "version": "7.736.0",
        "abi_version": 1,
        "package_type": "composite",
        "imports": [
            {"component": "orpheus.builtin.deinterleave"},
            {"component": "orpheus.builtin.fir"},
            {"component": "orpheus.builtin.mixer"},
            {"component": "orpheus.builtin.interleave"},
            {"component": "orpheus.builtin.delay_line"},
        ],
        "ports": [
            {"id": "in", "direction": "input", "type": "audio", "sample_format": "f32", "channels": input_channels, "maps_to": "split:in"},
            {"id": "out", "direction": "output", "type": "audio", "sample_format": "f32", "channels": output_count, "maps_to": "carry:out"},
        ],
        "parameters": [],
        "resources": {
            f"external_model.heq.{name}_fir": {
                "file": f"assets/{name}_fir.f32",
                "format": "f32le",
                "shape": [filter_count, 530],
                "sha256": metadata["coefficient_sha256"],
            },
            f"external_model.heq.{name}_iir": {
                "file": f"assets/{name}_iir.f32",
                "format": "f32le",
                "shape": [filter_count, 140],
                "sha256": metadata["iir_coefficient_sha256"],
            },
        },
        "graph": {"nodes": nodes, "connections": connections},
        "execution": {"latency_samples": 531, "realtime_safe": True},
    }


def write_node_notes(project_root: Path, documents: dict[str, dict], metadata: dict[str, dict]) -> None:
    notes = {
        "headrest_input": "10 路 Headrest 测试输入。",
        "headrest": "可展开的 10->40 路 HEQ 基础组件组合。",
        "overhead_input": "20 路 Overhead 测试输入。",
        "overhead": "可展开的 20-filter、四组求和 HEQ 基础组件组合。",
        "overhead_sink": "消费四路 Overhead 分组输出。",
    }
    for name, document in documents.items():
        component_id = document["id"]
        input_mapping = metadata[name]["input_mapping"]
        for node in document["graph"]["nodes"]:
            key = f"sub:{component_id}/{node['id']}"
            if node["id"] == "split":
                notes[key] = "把多通道输入拆为单通道 FIR 输入。"
            elif node["id"].startswith("fir_"):
                index = int(node["id"].split("_")[1])
                reference = node["params"]["coefficients"]
                notes[key] = (
                    f"第 {index} 个 530-tap FIR；输入通道 {input_mapping[index]}；"
                    f"资源切片 offset={reference['offset']} count={reference['count']}。"
                )
            elif node["id"].startswith("sum_"):
                notes[key] = "单通道二输入求和节点，属于五路 filter 的平衡 mixer 树。"
            elif node["id"] == "join":
                notes[key] = "把各单通道结果重新交错为多通道输出。"
            elif node["id"] == "carry":
                notes[key] = "复现生成实现的两样本跨块 carry。"
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
    component_path = output_dir.parent / f"{name}.component.yaml"
    component_path.write_text(
        yaml.safe_dump(composite_document(name, spec, metadata), sort_keys=False, allow_unicode=True),
        encoding="utf-8",
    )
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser(description="提取外部 Model_1_2 HEQ 默认资源并生成基础组件组合图")
    parser.add_argument("source_root", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    summary = {}
    documents = {}
    for name, spec in SPECS.items():
        summary[name] = extract_one(args.source_root, args.output_dir, name, spec)
        documents[name] = yaml.safe_load(
            (args.output_dir.parent / f"{name}.component.yaml").read_text(encoding="utf-8")
        )
    write_node_notes(args.output_dir.parents[2], documents, summary)
    print(json.dumps(summary, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())