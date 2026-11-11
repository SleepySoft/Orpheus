"""条件BULK在声明、数据目录和真实C壳中使用同一配置条件。"""
import json

import jsonschema
import pytest

from orpheus_core.component_codegen import generate_component
from orpheus_core.component_definition import ComponentDefinitionError, load_component_definition, normalize_component_definition
from orpheus_core.parameter_catalog import component_data_points
from test_component_codegen import ROOT, package, raw, save, _compile_component_harness


def conditional(raw):
    raw["interfaces"]["mode"] = {
        "type": "string", "default": "small", "capacity": 16, "update_policy": "restart_required"}
    raw["interfaces"]["gain"]["update_policy"] = "restart_required"
    raw["interfaces"]["enabled"]["update_policy"] = "restart_required"
    for key, width, conditions in (
        ("small", 4, {"mode": "small"}),
        ("large", 8, {"mode": "large", "enabled": True, "gain": 1}),
    ):
        raw["interfaces"][key] = {
            "type": "string", "default": "", "kind": "bulk", "update_policy": "restart_required",
            "shape": ["param:channels", width],
            "bulk": {"sample_format": "f32", "capacity": 8, "storage": "algorithm." + key,
                     "double_bank": True, "when": conditions},
        }
    return raw


@pytest.mark.parametrize("condition", [
    [], {"missing": "x"}, {"in": "x"}, {"large": ""},
    {"mode": 1}, {"channels": True}, {"channels": 99}, {"enabled": "true"},
    {"mode": "x" * 16},
])
def test_conditional_bulk_rejects_bad_conditions(package, raw, condition):
    conditional(raw)["interfaces"]["large"]["bulk"]["when"] = condition
    with pytest.raises(ComponentDefinitionError, match="when|条件"):
        load_component_definition(save(package, raw))


def test_conditional_bulk_rejects_mutable_selector(package, raw):
    conditional(raw)["interfaces"]["mode"]["update_policy"] = "block_boundary"
    with pytest.raises(ComponentDefinitionError, match="重启"):
        load_component_definition(save(package, raw))


def test_conditional_bulk_default_and_catalog_values(package, raw):
    source = conditional(raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(source, schema)
    view = normalize_component_definition(load_component_definition(save(package, source)))
    assert view["bulk_slots"][1]["when"] == {"mode": "large", "enabled": True, "gain": 1}
    for params, expected in (({}, ["small"]), ({"mode": "large"}, ["large"]),
                              ({"mode": "large", "enabled": False}, [])):
        points = component_data_points(view, params)
        assert [p["id"] for p in points if p.get("runtime")] == expected


@pytest.mark.parametrize("threshold,default,config_frames", [
    (1, 1, False), (.1, .10000000149011612, False), (1, 1, True),
])
def test_conditional_bulk_real_registration_and_active_capacity(package, raw, threshold, default, config_frames):
    conditional(raw)["interfaces"]["gain"]["default"] = default
    raw["interfaces"]["large"]["bulk"]["when"]["gain"] = threshold
    if config_frames:
        raw["interfaces"]["large"]["shape"] = ["config:frames"]
    path = save(package, raw)
    view = normalize_component_definition(load_component_definition(path))
    assert [p["id"] for p in component_data_points(view, {"mode": "large"}) if p.get("runtime")] == ["large"]
    generate_component(path)
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
typedef struct { unsigned count; const char* key; } Capture;
static OrpheusSlotId add(void* user, const OrpheusSlotInfo* slot) {
    Capture* capture=user;
    if(slot->kind==ORPHEUS_SLOT_BULK) {
        assert(slot->count==8 && (slot->flags & ORPHEUS_SLOT_DOUBLE_BUFFERED));
        capture->key=slot->key;capture->count++;
    }
    return 0;
}
int main(void) {
    test_pilot_State state;
    OrpheusConfig config={.channels=2,.state_block=&state};
    void* handle=0;
    const OrpheusComponentInterface* api=orpheus_get_interface();
    assert(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    Capture capture={0};OrpheusRegistry registry={.ctx=&capture,.add=add};
    assert(api->register_slots(handle,&registry)==0 && capture.count==1 && !strcmp(capture.key,"small"));
    const char* ids[]={"mode","enabled"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_STRING,.value.str="large"},
        {.type=ORPHEUS_VALUE_BOOL,.value.b=false}};
    config.param_ids=ids;config.param_values=params;config.param_count=2;
    assert(api->prepare(handle,&config)==0);
    capture.count=0;
    assert(api->register_slots(handle,&registry)==0 && capture.count==0);
    params[1].value.b=true;
    assert(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    assert(!state.enabled && !strcmp(state.mode,"large"));
    config.channels=1;config.block_size=8;
    assert(api->prepare(handle,&config)==0);
    assert(api->register_slots(handle,&registry)==0 && capture.count==1 && !strcmp(capture.key,"large"));
    assert(api->destroy(handle)==0);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)


def test_conditional_bulk_validates_f32_matching_active_default_capacity(package, raw):
    conditional(raw)["interfaces"]["mode"]["default"] = "large"
    raw["interfaces"]["gain"]["default"] = .10000000149011612
    raw["interfaces"]["large"]["bulk"]["when"]["gain"] = .1
    with pytest.raises(ComponentDefinitionError, match="默认 shape"):
        load_component_definition(save(package, raw))


@pytest.mark.parametrize("value", ["invalid", float("nan"), float("inf"), 1e100])
def test_conditional_bulk_invalid_f32_catalog_values_have_explicit_diagnostics(package, raw, value):
    view = normalize_component_definition(load_component_definition(save(package, conditional(raw))))
    with pytest.raises(ComponentDefinitionError, match="有效f32"):
        component_data_points(view, {"mode": "large", "gain": value})


@pytest.mark.parametrize("source_mode,target_mode", [
    ("shared", "banked"), ("banked", "shared"), ("banked", "banked"),
])
def test_conditional_bulk_control_links_require_active_endpoints(source_mode, target_mode):
    from orpheus_core.compiler import CompileError, GraphCompiler
    from orpheus_core.project import Connection, ControlConnection, Graph, Node, PortRef, Project
    from orpheus_core.parameter_catalog import build_catalog
    from orpheus_core.registry import Registry

    registry = Registry([ROOT / "components"])
    registry.scan()
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "clock": Node("clock", "orpheus.builtin.signal_gen", params={"channels": 2}),
        "a": Node("a", "orpheus.builtin.fir_bank", params={"coefs_mode": source_mode, "num_filters": 2}),
        "b": Node("b", "orpheus.builtin.fir_bank", params={"coefs_mode": target_mode, "num_filters": 2}),
    }, connections=[
        Connection(PortRef("clock", "out"), PortRef("a", "in")),
        Connection(PortRef("clock", "out"), PortRef("b", "in")),
    ]), control_connections=[
        ControlConnection(PortRef("a", "filter_indices_i32"), PortRef("b", "filter_indices_i32")),
    ])
    catalog = {entry.node_id: entry for entry in build_catalog(project, registry)}
    for key, mode in (("a", source_mode), ("b", target_mode)):
        points = catalog[key].by_kind["bulk"]
        expected = {"coefs_banked", "filter_indices_i32"} if mode == "banked" else {"coefs"}
        assert {point["id"] for point in points} == expected and len(points) == len(expected)
    if "shared" in (source_mode, target_mode):
        with pytest.raises(CompileError, match="未启用"):
            GraphCompiler(registry).compile(project)
    else:
        plan = GraphCompiler(registry).compile(project)
        assert len(plan.control_links) == 1 and plan.control_links[0]["count"] == 2


@pytest.mark.parametrize("mode,channels,filters,taps", [
    ("per_channel", 32, 1, 1025), ("banked", 2, 64, 513),
])
def test_conditional_bulk_compiler_rejects_active_overflow(mode, channels, filters, taps):
    from orpheus_core.compiler import CompileError, GraphCompiler
    from orpheus_core.project import Connection, Graph, Node, PortRef, Project
    from orpheus_core.registry import Registry

    registry = Registry([ROOT / "components"])
    registry.scan()
    project = Project(flow_mode="task_sequence", graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.wav_in", params={"channels": channels, "file_path": "input.wav"}),
        "bank": Node("bank", "orpheus.builtin.fir_bank", params={
            "channels": channels, "coefs_mode": mode, "num_filters": filters, "num_taps": taps}),
    }, connections=[Connection(PortRef("source", "out"), PortRef("bank", "in"))]))
    with pytest.raises(CompileError, match="容量"):
        GraphCompiler(registry).compile(project)
