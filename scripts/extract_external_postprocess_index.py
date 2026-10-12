#!/usr/bin/env python3
"""Extract indexed Runtime A PostProcess defaults without generating a graph."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from extract_external_big6_index import normalized_digest, scalar_for_field, sha256
from extract_external_input_index import float_array, integer_array


ARRAY_FIELDS = {
    "routing.main_select": ("ASDRouterMainSelect", 22, True),
    "routing.aux_select": ("AuxSelect", 22, True),
    "freq_comp.coefficients": ("FreqCompxpooliirCoeffs", 175, False),
    "freq_comp.stage_count": ("FreqCompxPooliirNumStages", 22, True),
    "limiter.attack.high": ("Limiterattack", 22, False),
    "limiter.attack.low": ("Limiterattack_low", 22, False),
    "limiter.decay.high": ("Limiterdecay", 22, False),
    "limiter.decay.low": ("Limiterdecay_low", 22, False),
    "limiter.k1.high": ("Limiterk1", 22, False),
    "limiter.k1.low": ("Limiterk1_low", 22, False),
    "limiter.max_attack.high": ("LimitermaxAttack", 22, False),
    "limiter.max_attack.low": ("LimitermaxAttack_low", 22, False),
    "post_eq.coefficients": ("OutputEQEQpooliirCoeffs", 560, False),
    "post_eq.stage_count": ("OutputEQEQPooliirNumStages", 22, True),
    "test_mode.matrix": ("TestMode", 220, False),
    "output_delay.samples": ("TunablePoolDelayChanDelays", 22, True),
}

SCALAR_FIELDS = {
    "routing.aux_enabled": ("ASDRouterEnableAux", True),
    "all_mute.ramp_ms": ("AllMuteRampTime", False),
    "mute.map_switch": ("MuteMapSwitch", True),
    "mute.ramp_db_per_second": ("MuteRampRate", False),
    "mute.ramp_ms": ("MuteRampTime", False),
    "rmdl_mute.ramp_ms": ("RmdlMuteRampTime", False),
    "soft_clip_internal.high.p2": ("SoftClipperp2", False),
    "soft_clip_internal.high.xmax": ("SoftClipperxmax", False),
    "soft_clip_internal.high.xmin": ("SoftClipperxmin", False),
    "soft_clip_internal.low.p2": ("SoftClipperp2_low", False),
    "soft_clip_internal.low.xmax": ("SoftClipperxmax_low", False),
    "soft_clip_internal.low.xmin": ("SoftClipperxmin_low", False),
    "soft_clip_output.high.p2": ("OutputSclipp2", False),
    "soft_clip_output.high.xmax": ("OutputSclipxmax", False),
    "soft_clip_output.high.xmin": ("OutputSclipxmin", False),
    "soft_clip_output.low.p2": ("OutputSclipp2_low", False),
    "soft_clip_output.low.xmax": ("OutputSclipxmax_low", False),
    "soft_clip_output.low.xmin": ("OutputSclipxmin_low", False),
}


def nested_set(document: dict, dotted_key: str, value: object) -> None:
    target = document
    parts = dotted_key.split(".")
    for part in parts[:-1]:
        target = target.setdefault(part, {})
    target[parts[-1]] = value


def array_fact(field: str, values: list[int | float]) -> dict:
    return {
        "source_field": field,
        "count": len(values),
        "sha256": normalized_digest(values),
        "values": values,
    }


def extract(source_root: Path) -> dict:
    top_path = source_root / "Model_1_1_PostProcess_p0_b0_TOP.c"
    inner_path = source_root / "PostProcess.c"
    outer_path = source_root / "Model_1_1.c"
    source = top_path.read_text(encoding="utf-8")
    facts: dict = {
        "channels": 22,
        "stage_order": [
            "internal_gain_ramp",
            "limiter",
            "post_eq",
            "internal_soft_clip",
            "rmdl_mute",
            "asd_main_aux_router",
            "all_mute",
            "output_soft_clip",
            "output_calibration",
            "freq_comp",
            "output_delay",
        ],
        "routing": {
            "input_boundary": "32-channel PreAmp post buffer plus internal/aux sources",
            "rtc_sentinel": "RTC entries with bit 31 set retain TOP values",
        },
        "output_calibration": {
            "parameter_source": "runtime state / RTC",
            "default_values": "pending",
        },
    }
    for key, (field, count, as_integer) in ARRAY_FIELDS.items():
        values: list[int | float]
        if as_integer:
            values = integer_array(source, field, count)
        else:
            values = float_array(source, field, count)
        nested_set(facts, key, array_fact(field, values))
    for key, (field, as_integer) in SCALAR_FIELDS.items():
        value: int | float = scalar_for_field(source, field)
        if as_integer:
            value = int(value)
        nested_set(facts, key, {"source_field": field, "value": value})
    return {
        "schema_version": 1,
        "module_ids": ["A.POST_PROCESS"],
        "sources": [
            {"file": top_path.name, "sha256": sha256(top_path)},
            {"file": inner_path.name, "sha256": sha256(inner_path)},
            {"file": outer_path.name, "sha256": sha256(outer_path)},
        ],
        "facts": facts,
        "scope": "default TOP arrays and stage ownership; runtime calibration, RTC state transitions and golden remain pending",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="提取双 Runtime 模型 PostProcess 索引证据")
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
