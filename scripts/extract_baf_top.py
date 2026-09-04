#!/usr/bin/env python3
"""Extract one generated BAF TOP array by its field comment."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

_NUMBER = re.compile(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?[Ff]?")


def initializer_for_field(source: str, field: str) -> str:
    marker = re.search(rf"/\*\s*{re.escape(field)}\s*\*/", source)
    if marker is None:
        raise ValueError(f"field comment not found: {field}")
    opening = source.find("{", marker.end())
    if opening < 0:
        raise ValueError(f"initializer not found after field: {field}")
    depth = 0
    for index in range(opening, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise ValueError(f"unterminated initializer for field: {field}")


def extract_float_array(source: str, field: str) -> list[float]:
    initializer = initializer_for_field(source, field)
    return [float(token.rstrip("Ff")) for token in _NUMBER.findall(initializer)]


def main() -> int:
    parser = argparse.ArgumentParser(description="提取 Simulink Coder BAF TOP 数组")
    parser.add_argument("file", type=Path)
    parser.add_argument("field")
    parser.add_argument("--expect-count", type=int)
    parser.add_argument("--format", choices=("json", "csv", "summary"), default="summary")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    source = args.file.read_text(encoding="utf-8")
    values = extract_float_array(source, args.field)
    if args.expect_count is not None and len(values) != args.expect_count:
        raise SystemExit(
            f"{args.field}: expected {args.expect_count} values, extracted {len(values)}"
        )
    digest = hashlib.sha256(",".join(format(value, ".9g") for value in values).encode("ascii")).hexdigest()
    if args.format == "json":
        rendered = json.dumps(values, ensure_ascii=False, separators=(",", ":"))
    elif args.format == "csv":
        rendered = ",".join(format(value, ".9g") for value in values)
    else:
        rendered = json.dumps({
            "file": str(args.file),
            "field": args.field,
            "count": len(values),
            "sha256": digest,
            "first": values[:4],
            "last": values[-4:],
        }, ensure_ascii=False, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    else:
        print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
