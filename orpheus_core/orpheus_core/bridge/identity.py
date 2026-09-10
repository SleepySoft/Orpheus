"""Bridge 身份摘要：compiler 生成，所有 Backend 只消费。"""

from __future__ import annotations

import json
import struct
from typing import Any

_KIND_VALUES = {"RTC": 0, "TUNE": 1, "PROBE": 2, "STATE": 3, "CUSTOM": 4}
_FORM_VALUES = {"SCALAR": 0, "BULK": 1, "MODULE": 2}
_TYPE_VALUES = {"float": 0, "int": 1, "bool": 2, "string": 3, "bulk_ref": 4}
_TYPE_SIZES = {"float": 4, "int": 4, "bool": 1, "string": 1, "bulk_ref": 4}


def fnv1a64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def wire_map_entries(id_map: list[dict[str, Any]]) -> list[tuple[int, ...]]:
    output = []
    for entry in id_map:
        data_id = int(entry["id"])
        count = int(entry.get("count", 1) or 1)
        data_type = entry.get("type", "float")
        byte_size = 0 if entry["kind"] == "CUSTOM" else count * _TYPE_SIZES[data_type]
        output.append((
            data_id,
            _KIND_VALUES[entry["kind"]],
            _FORM_VALUES[entry["form"]],
            _TYPE_VALUES[data_type],
            count,
            byte_size,
            (data_id >> 16) & 0xFF,
            data_id & 0xFFFF,
        ))
    return output


def build_identity(plan) -> dict[str, int]:
    graph_payload = {
        "sample_rate": plan.sample_rate,
        "block_size": plan.block_size,
        "target": plan.target,
        "nodes": plan.node_configs,
        "connections": plan.connections,
        "control_links": plan.control_links,
        "schedule": plan.schedule,
    }
    plan_payload = {
        **graph_payload,
        "tasks": plan.tasks,
        "buffers": plan.buffers,
        "modules": plan.modules,
        "bridges": plan.bridges,
        "duration_frames": plan.duration_frames,
    }
    map_bytes = b"".join(
        struct.pack("<8I", *entry) for entry in wire_map_entries(plan.id_map)
    )
    encode = lambda value: json.dumps(
        value, sort_keys=True, ensure_ascii=False, separators=(",", ":")
    ).encode("utf-8")
    return {
        "graph_hash": fnv1a64(encode(graph_payload)),
        "plan_hash": fnv1a64(encode(plan_payload)),
        "id_map_hash": fnv1a64(map_bytes),
    }
