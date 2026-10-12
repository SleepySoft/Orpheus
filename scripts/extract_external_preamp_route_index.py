#!/usr/bin/env python3
"""Extract indexed Runtime A PreAmp routing and level-detection defaults."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from extract_external_big6_index import normalized_digest, scalar_for_field, sha256
from extract_external_input_index import array_fact, float_array, integer_array


LEVEL_FIELDS = {
    "audiopilot": {
        "fast_decay_db_per_sample": "AudiopilotLevelDetectFastDecayDbPerSample",
        "hold_margin_db": "AudiopilotLevelDetectHoldMarginDb",
        "hold_time_samples": "AudiopilotLevelDetectHoldTimeSamples",
        "maximum_volume_db": "AudiopilotLevelDetectMaxVolumeDb",
        "minimum_volume_db": "AudiopilotLevelDetectMinVolumeDb",
        "slow_decay_db_per_sample": "AudiopilotLevelDetectSlowDecayDbPerSample",
    },
    "dynamic_eq": {
        "fast_decay_db_per_sample": "DyneqLevelDetectFastDecayDbPerSample",
        "hold_margin_db": "DyneqLevelDetectHoldMarginDb",
        "hold_time_samples": "DyneqLevelDetectHoldTimeSamples",
        "maximum_volume_db": "DyneqLevelDetectMaxVolumeDb",
        "minimum_volume_db": "DyneqLevelDetectMinVolumeDb",
        "slow_decay_db_per_sample": "DyneqLevelDetectSlowDecayDbPerSample",
    },
}


def extract(source_root: Path) -> dict:
    top_path = source_root / "Model_1_1_PreAmp_p0_b0_TOP.c"
    source = top_path.read_text(encoding="utf-8")
    route = integer_array(source, "PreqRouterOut1OutMap", 32)
    coefficients = float_array(source, "LevelDetectEntLevPreEmpFilterpooliirCoeffs", 50)
    stages = integer_array(source, "LevelDetectEntLevPreEmpFilterPooliirNumStages", 10)
    level_modes = {
        mode: {
            key: {
                "source_field": field,
                "value": scalar_for_field(source, field),
            }
            for key, field in fields.items()
        }
        for mode, fields in LEVEL_FIELDS.items()
    }
    return {
        "schema_version": 1,
        "module_ids": ["A.PREAMP_ROUTE_LEVEL"],
        "sources": [{"file": top_path.name, "sha256": sha256(top_path)}],
        "facts": {
            "branch_provenance": {
                "post_buffer_32": {
                    "source": "volume_audio_10",
                    "purpose": "feed Runtime A final PostProcess",
                    "important": "bypasses Bass/Treble/Mid/Balance in the current frame",
                },
                "adaptive_buffer_10": {
                    "source": "balanced_audio_10",
                    "purpose": "feed adaptive loudness and Runtime B",
                },
                "microphone_buffer_1": {
                    "source": "input_select.microphone_1",
                    "purpose": "feed adaptive noise estimation",
                },
                "level_detection_audio_10": {
                    "source": "input_mixer.program_10",
                    "purpose": "measure pre-Big6 program after delay and pre-emphasis",
                    "volume_compensation": "uses Volume current gain converted to dB",
                },
            },
            "post_buffer_route": {
                "input_channels": 10,
                "output_channels": 32,
                "default_map": array_fact("PreqRouterOut1OutMap", route),
                "rtc_override": "each nonzero RTC entry replaces the same TOP route entry",
                "valid_source_channels": [1, 2, 3, 4, 5, 6],
                "invalid_route_rule": "route <= 0 or route > 6 produces an all-zero output channel",
            },
            "level_detection": {
                "channels": 10,
                "delay_samples": {
                    "source_field": "LevelDetectMusicDelaySamples",
                    "value": int(scalar_for_field(source, "LevelDetectMusicDelaySamples")),
                },
                "pre_emphasis_coefficients": array_fact(
                    "LevelDetectEntLevPreEmpFilterpooliirCoeffs", coefficients
                ),
                "pre_emphasis_stages": array_fact(
                    "LevelDetectEntLevPreEmpFilterPooliirNumStages", stages
                ),
                "system_max_dbspl": {
                    "source_field": "LevelDetectSysMaxDbspl",
                    "value": scalar_for_field(source, "LevelDetectSysMaxDbspl"),
                },
                "frame_metrics": ["maximum channel RMS in dB", "maximum absolute sample in dB"],
                "modes": level_modes,
            },
        },
        "scope": "default routes, branch provenance and detector parameters; InnerLink state machine golden remains pending",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="提取双 Runtime 模型 PreAmp 路由/电平索引证据")
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
