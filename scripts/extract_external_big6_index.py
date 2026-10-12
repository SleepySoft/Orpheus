#!/usr/bin/env python3
"""Extract indexed Big6 defaults without constructing an Orpheus graph."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

from extract_external_top import extract_float_array


NUMBER = re.compile(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?[FfUu]?")

ARRAY_FIELDS = {
    "balance.channel_to_ramper": ("BalanceChanRampMap", 10, True),
    "balance.table_db": ("BalanceTableDb", 30, False),
    "balance.table_index": ("BalanceTableIdx", 30, True),
    "bass.boost_coefficients": ("BassBoostCoeffs", 3, False),
    "bass.cut_coefficients": ("BassCutCoeffs", 3, False),
    "mid.boost_coefficients": ("MidrangeBoostCoeffs", 5, False),
    "mid.cut_coefficients": ("MidrangeCutCoeffs", 5, False),
    "treble.boost_coefficients": ("TrebleBoostCoeffs", 3, False),
    "treble.cut_coefficients": ("TrebleCutCoeffs", 3, False),
    "volume.table_db": ("VolumeTable_dB", 8, False),
    "volume.table_index": ("VolumeTable_Idx", 8, True),
}

SCALAR_FIELDS = {
    "balance.boost_disabled": ("BalanceBoostDisable", True),
    "balance.silent_extreme": ("BalanceEnableSilentExtreme", True),
    "balance.offset": ("BalanceOffset", False),
    "balance.ramp_ms": ("BalanceRampTime", False),
    "balance.extreme_mutes_bass": ("BalanceSleepingBeautyMutesBass", True),
    "bass.maximum_boost_db": ("BassBoostMaximum_dB", False),
    "bass.maximum_cut_db": ("BassCutMaximum_dB", False),
    "bass.offset": ("BassOffset", False),
    "bass.scale": ("BassScale", False),
    "bass.ramp_ms": ("Bassramptime", False),
    "mid.maximum_boost_db": ("MidrangeBoostMaximum_dB", False),
    "mid.maximum_cut_db": ("MidrangeCutMaximum_dB", False),
    "mid.offset": ("MidrangeOffset", False),
    "mid.scale": ("MidrangeScale", False),
    "mid.ramp_ms": ("MidrangeRampTime", False),
    "treble.maximum_boost_db": ("TrebleBoostMaximum_dB", False),
    "treble.maximum_cut_db": ("TrebleCutMaximum_dB", False),
    "treble.offset": ("TrebleOffset", False),
    "treble.scale": ("TrebleScale", False),
    "treble.ramp_ms": ("Trebleramptime", False),
    "volume.ramp_ms": ("VolumeRampTime", False),
    "volume.maximum_gain_db": ("Volumemaxgain", False),
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scalar_for_field(source: str, field: str) -> float:
    marker = re.search(rf"/\*\s*{re.escape(field)}\s*\*/", source)
    if marker is None:
        raise ValueError(f"field comment not found: {field}")
    next_marker = source.find("/*", marker.end())
    segment = source[marker.end(): next_marker if next_marker >= 0 else len(source)]
    token = NUMBER.search(segment)
    if token is None:
        raise ValueError(f"scalar initializer not found: {field}")
    return float(token.group(0).rstrip("FfUu"))


def volume_channel_map(source: str) -> list[int]:
    match = re.search(
        r"\{\s*\{(?P<values>[^{}]+)\}\s*/\*\s*'<S517>/Audio Stats Func'\s*\*/",
        source,
    )
    if match is None:
        raise ValueError("Volume channel map initializer not found")
    values = [int(float(token.rstrip("FfUu"))) for token in NUMBER.findall(match.group("values"))]
    if len(values) != 11:
        raise ValueError(f"Volume channel map: expected 11 values, got {len(values)}")
    if values[0] != 10 or values[1:] != list(range(1, 11)):
        raise ValueError(f"unexpected Volume channel map: {values}")
    return values


def normalized_digest(values: list[int | float]) -> str:
    payload = ",".join(format(value, ".9g") for value in values).encode("ascii")
    return hashlib.sha256(payload).hexdigest()


def nested_set(document: dict, dotted_key: str, value: object) -> None:
    target = document
    parts = dotted_key.split(".")
    for part in parts[:-1]:
        target = target.setdefault(part, {})
    target[parts[-1]] = value


def extract(source_root: Path) -> dict:
    top_path = source_root / "Model_1_1_PreAmp_p0_b0_TOP.c"
    constants_path = source_root / "Model_1_1_data.c"
    top_source = top_path.read_text(encoding="utf-8")
    constants_source = constants_path.read_text(encoding="utf-8")
    facts: dict = {
        "processing_order": ["volume", "bass", "treble", "mid", "balance"],
        "volume": {"channels": 10, "ramper_count": 1},
        "balance": {"channels": 10, "ramper_count": 4},
    }

    for key, (field, expected_count, as_integer) in ARRAY_FIELDS.items():
        values: list[int | float] = extract_float_array(top_source, field)
        if as_integer:
            integers = [int(value) for value in values]
            if any(float(integer) != value for integer, value in zip(integers, values)):
                raise ValueError(f"{field}: expected integer values")
            values = integers
        if len(values) != expected_count:
            raise ValueError(f"{field}: expected {expected_count} values, got {len(values)}")
        nested_set(facts, key, {
            "source_field": field,
            "count": len(values),
            "sha256": normalized_digest(values),
            "values": values,
        })

    for key, (field, as_integer) in SCALAR_FIELDS.items():
        value: int | float = scalar_for_field(top_source, field)
        if as_integer:
            value = int(value)
        nested_set(facts, key, {"source_field": field, "value": value})

    mapping = volume_channel_map(constants_source)
    facts["volume"]["channel_map"] = {
        "source_anchor": "<S517>/Audio Stats Func",
        "count": len(mapping),
        "sha256": normalized_digest(mapping),
        "values": mapping,
        "encoding": "first value is channel count; remaining values are 1-based channels",
    }
    return {
        "schema_version": 1,
        "module_ids": ["A.BIG6_VOLUME", "A.BIG6_TONE", "A.BIG6_BALANCE"],
        "sources": [
            {"file": top_path.name, "sha256": sha256(top_path)},
            {"file": constants_path.name, "sha256": sha256(constants_path)},
        ],
        "facts": facts,
        "scope": "defaults and layouts only; process equations and golden validation remain separate gates",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="提取双 Runtime 模型 Big6 索引证据")
    parser.add_argument("source_root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    document = extract(args.source_root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "output": str(args.output),
        "modules": document["module_ids"],
        "sources": document["sources"],
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
