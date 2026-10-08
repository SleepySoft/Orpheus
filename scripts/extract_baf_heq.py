#!/usr/bin/env python3
"""Extract Model_1_2 Headrest/Overhead HEQ defaults into directory-project resources."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_baf_top import extract_float_array  # noqa: E402


SPECS = {
    "headrest": {
        "metadata_file": "Model_1_2_PreAmp_p10_b0_TOP.c",
        "coefficient_file": "Model_1_2_PreAmp_p10_b1_TOP.c",
        "prefix": "MedusaHeadrestCompEq",
        "filter_count": 40,
        "output_count": 40,
        "coefficient_count": 21200,
        "iir_coefficient_count": 5600,
    },
    "overhead": {
        "metadata_file": "Model_1_2_PreAmp_p9_b0_TOP.c",
        "coefficient_file": "Model_1_2_PreAmp_p9_b1_TOP.c",
        "prefix": "MedusaOverheadHeq",
        "filter_count": 20,
        "output_count": 4,
        "coefficient_count": 10600,
        "iir_coefficient_count": 2800,
    },
}


def extract_field(path: Path, field: str, count: int) -> list[float]:
    values = extract_float_array(path.read_text(encoding="utf-8"), field)
    if len(values) != count:
        raise ValueError(f"{field}: expected {count} values, got {len(values)}")
    return values


def integers(values: list[float], field: str) -> list[int]:
    result = [int(value) for value in values]
    if any(float(integer) != value for integer, value in zip(result, values)):
        raise ValueError(f"{field}: contains non-integer values")
    return result


def extract_one(source_root: Path, output_dir: Path, name: str, spec: dict) -> dict:
    metadata_path = source_root / spec["metadata_file"]
    coefficient_path = source_root / spec["coefficient_file"]
    prefix = spec["prefix"]
    filter_count = spec["filter_count"]
    output_count = spec["output_count"]
    fields = {
        "filter_lengths": integers(
            extract_field(metadata_path, f"{prefix}FirCoeffsLengths", filter_count),
            "filter_lengths",
        ),
        "coefficient_mapping": integers(
            extract_field(metadata_path, f"{prefix}FirCoeffsMapping", filter_count),
            "coefficient_mapping",
        ),
        "input_mapping": integers(
            extract_field(metadata_path, f"{prefix}FirInputMapping", filter_count),
            "input_mapping",
        ),
        "fir_delays": integers(
            extract_field(metadata_path, f"{prefix}FirDelays", filter_count),
            "fir_delays",
        ),
        "output_starts": integers(
            extract_field(metadata_path, f"{prefix}OutputMapping", output_count),
            "output_starts",
        ),
        "iir_num_stages": integers(
            extract_field(
                metadata_path,
                f"{prefix}PoolIirPooliirNumStages",
                filter_count,
            ),
            "iir_num_stages",
        ),
    }
    coefficients = extract_field(
        coefficient_path,
        f"{prefix}FirCoeffsTarget",
        spec["coefficient_count"],
    )
    if sum(fields["filter_lengths"]) != len(coefficients):
        raise ValueError(f"{name}: filter lengths do not sum to coefficient count")
    iir_coefficients = extract_field(
        metadata_path,
        f"{prefix}PoolIirpooliirCoeffs",
        spec["iir_coefficient_count"],
    )
    raw = struct.pack(f"<{len(coefficients)}f", *coefficients)
    iir_raw = struct.pack(f"<{len(iir_coefficients)}f", *iir_coefficients)
    output_dir.mkdir(parents=True, exist_ok=True)
    coefficient_output = output_dir / f"{name}_fir.f32"
    coefficient_output.write_bytes(raw)
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
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser(description="提取 BAF Model_1_2 HEQ 默认资源")
    parser.add_argument("source_root", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    summary = {
        name: extract_one(args.source_root, args.output_dir, name, spec)
        for name, spec in SPECS.items()
    }
    print(json.dumps(summary, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())