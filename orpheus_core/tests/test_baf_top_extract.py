"""BAF TOP generated-C array extraction tests."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from extract_baf_top import extract_float_array  # noqa: E402


def test_extract_float_array_uses_balanced_initializer() -> None:
    source = """
    SomeType value = {
        /* Other */ 12,
        /* Weights */
        {
            1.0F, -2.5e-3F,
            { 3, 4.25F }
        },
        /* Tail */ 99
    };
    """
    assert extract_float_array(source, "Weights") == [1.0, -0.0025, 3.0, 4.25]


def test_extract_float_array_rejects_missing_field() -> None:
    with pytest.raises(ValueError, match="field comment not found"):
        extract_float_array("int x = 1;", "Weights")
