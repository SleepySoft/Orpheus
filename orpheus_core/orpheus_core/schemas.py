"""Load JSON schemas and validate YAML/JSON data."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import jsonschema


_SCHEMA_DIR = Path(__file__).parent / "schemas"


def _load_schema(name: str) -> dict[str, Any]:
    path = _SCHEMA_DIR / name
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_component_manifest_schema() -> dict[str, Any]:
    return _load_schema("component_manifest.schema.json")


def load_project_schema() -> dict[str, Any]:
    return _load_schema("project.schema.json")


def validate(data: Any, schema: dict[str, Any]) -> None:
    jsonschema.validate(instance=data, schema=schema)
