"""第二批六组件的接口事实源、交付编号与控制元数据约束。"""

from pathlib import Path

import pytest

from orpheus_core.component_definition import (
    load_component_definition,
    normalize_component_definition,
)

ROOT = Path(__file__).resolve().parents[2]
COMPONENTS = ("balance", "mute", "square", "saturation", "soft_clipper", "exponential_gain")


@pytest.mark.parametrize("name", COMPONENTS)
def test_common_component_uses_interfaces_and_generated_shell(name):
    directory = ROOT / "components" / "orpheus" / "builtin" / name
    raw = load_component_definition(directory / "component.yaml")
    assert not ({"ports", "parameters", "sources", "headers", "state_type"} & raw.keys())
    assert raw["abi_version"] == 7
    assert raw["algorithm"]["lifecycle"] == ["prepare", "reset", "set_parameter"]
    definition = normalize_component_definition(raw)
    assert definition["sources"] == ["generated/component_shell.c", "user/algorithm.c"]
    assert definition["state_type"] == f"orpheus_builtin_{name}_State"
    assert [port["id"] for port in definition["ports"]] == ["in", "out"]
    inlet, outlet = definition["ports"]
    assert inlet["process"] and inlet["channels"] == "param:channels"
    assert outlet["drive_port_no"] == 1 and outlet["drive_divide"] == 1
    assert outlet["channels"] == "param:channels"
    assert definition["memory"]["alignment"] == 8
    assert definition["execution"]["supports_inplace"]
    assert definition["execution"]["realtime_safe"]
    assert (directory / "user" / "algorithm.h").is_file()
    source = (directory / "user" / "algorithm.c").read_text(encoding="utf-8")
    assert f"orpheus_builtin_{name}_emit_out(ctx, &io->out)" in source
    assert "OrpheusComponentDescriptor" not in source
    assert "orpheus_process_next" not in source and "orpheus_process_branch" not in source
    assert not (directory / "src" / f"{name}.c").exists()


def test_exponential_gain_keeps_control_sources_and_optional_pins():
    raw = load_component_definition(
        ROOT / "components" / "orpheus" / "builtin" / "exponential_gain" / "component.yaml"
    )
    interfaces = raw["interfaces"]
    assert interfaces["in"]["optional"] and interfaces["out"]["optional"]
    for key in ("target_gain", "ramp_coefficient", "remaining_blocks"):
        assert interfaces[key]["bindable"] and interfaces[key]["readback"]
        assert interfaces[key]["persistent"]
        assert interfaces[key]["update_policy"] == "block_boundary"
    assert interfaces["remaining_blocks"]["control_source"]
    probe = interfaces["current_gain"]
    assert probe["kind"] == "probe" and probe["readonly"] and probe["readback"]
    assert probe["control_source"] and not probe["persistent"]
