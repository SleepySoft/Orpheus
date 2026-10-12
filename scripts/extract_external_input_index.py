#!/usr/bin/env python3
"""Extract indexed Runtime A input routing defaults without generating a graph."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from extract_external_big6_index import normalized_digest, scalar_for_field, sha256
from extract_external_top import extract_float_array


def integer_array(source: str, field: str, expected_count: int) -> list[int]:
    values = extract_float_array(source, field)
    integers = [int(value) for value in values]
    if len(integers) != expected_count:
        raise ValueError(f"{field}: expected {expected_count} values, got {len(integers)}")
    if any(float(integer) != value for integer, value in zip(integers, values)):
        raise ValueError(f"{field}: expected integer values")
    return integers


def float_array(source: str, field: str, expected_count: int) -> list[float]:
    values = extract_float_array(source, field)
    if len(values) != expected_count:
        raise ValueError(f"{field}: expected {expected_count} values, got {len(values)}")
    return values


def array_fact(field: str, values: list[int | float]) -> dict:
    return {
        "source_field": field,
        "count": len(values),
        "sha256": normalized_digest(values),
        "values": values,
    }


def extract(source_root: Path) -> dict:
    select_path = source_root / "Model_1_1_InputSelect_p0_b0_TOP.c"
    preamp_path = source_root / "Model_1_1_PreAmp_p0_b0_TOP.c"
    select_source = select_path.read_text(encoding="utf-8")
    preamp_source = preamp_path.read_text(encoding="utf-8")

    route = integer_array(select_source, "routerOutMap", 30)
    announcement = integer_array(select_source, "AnnSelect", 17)
    entertainment = integer_array(select_source, "EntSelect", 12)
    microphone = int(scalar_for_field(select_source, "MicSelect"))
    weights_514 = float_array(preamp_source, "InputMixer3dWeights_514", 3)
    weights_lr = float_array(preamp_source, "InputMixer3dWeights_L_R", 8)
    stereo_mode = bool(int(scalar_for_field(preamp_source, "InputMixer3dStereoDnMix")))
    makeup_gain = scalar_for_field(preamp_source, "MakeupGainMakeupGain")

    return {
        "schema_version": 1,
        "module_ids": ["A.INPUT_SELECT", "A.INPUT_MIXER"],
        "sources": [
            {"file": select_path.name, "sha256": sha256(select_path)},
            {"file": preamp_path.name, "sha256": sha256(preamp_path)},
        ],
        "facts": {
            "input_select": {
                "input_channels": 30,
                "frame_size": 32,
                "default_route": array_fact("routerOutMap", route),
                "rtc_override": {
                    "entry_count": 30,
                    "rule": "each nonzero RTC entry replaces the same TOP route entry",
                    "invalid_route_rule": "route <= 0 or route > 30 produces an all-zero output channel",
                },
                "entertainment": array_fact("EntSelect", entertainment),
                "announcement": array_fact("AnnSelect", announcement),
                "microphone": {"source_field": "MicSelect", "value": microphone},
                "selection_order": "apply 30-channel route first, then select entertainment/announcement/microphone",
            },
            "input_mixer": {
                "input_channels": 12,
                "output_channels": 10,
                "input_order": ["L", "R", "C", "LFE", "Lss", "Rss", "Lsr", "Rsr", "Ltf", "Ltb", "Rtf", "Rtb"],
                "output_514_order": ["L", "R", "C", "LFE", "Lss", "Rss", "Ltf", "Ltb", "Rtf", "Rtb"],
                "makeup_gain": {"source_field": "MakeupGainMakeupGain", "value": makeup_gain},
                "weights_514": {
                    **array_fact("InputMixer3dWeights_514", weights_514),
                    "labels": ["LFE gain", "Lsr to Lss", "Rsr to Rss"],
                },
                "weights_stereo": {
                    **array_fact("InputMixer3dWeights_L_R", weights_lr),
                    "labels": ["Lss to L", "Ltf to L", "Ltb to L", "Rss to R", "Rtf to R", "Rtb to R", "C to L/R", "weighted LFE to L/R"],
                },
                "stereo_downmix_default": {
                    "source_field": "InputMixer3dStereoDnMix",
                    "value": stereo_mode,
                    "false_path": "5.1.4 output",
                    "true_path": "stereo in channels 1/2 with channels 3..10 zero",
                },
            },
        },
        "scope": "default routes, weights and switch semantics only; RTC transaction and golden validation remain separate gates",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="提取双 Runtime 模型输入路由索引证据")
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
