"""生成控制面必须遵守参数更新策略及组件调用边界。"""
from pathlib import Path
import subprocess

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry

ROOT = Path(__file__).resolve().parents[2]


def test_generated_scalar_settings_honor_component_contract(tmp_path):
    from test_generated_double_bank import _build

    registry = Registry()
    registry.add_search_path(ROOT / "components")
    registry.scan()
    project = Project(metadata={"name": "参数调用边界"})
    project.tasks = {"default": Task(id="default", sample_rate=48000, block_size=32)}
    project.graph = Graph(nodes={
        "source": Node(id="source", component="orpheus.builtin.signal_gen",
                       params={"channels": 1}),
        "down": Node(id="down", component="orpheus.builtin.resample",
                     params={"channels": 1, "factor": 2}),
        "gain": Node(id="gain", component="orpheus.builtin.gain", params={"channels": 1}),
    }, connections=[
        Connection(from_ref=PortRef("source", "out"), to_ref=PortRef("down", "in")),
        Connection(from_ref=PortRef("down", "out"), to_ref=PortRef("gain", "in")),
    ])
    plan = GraphCompiler(registry).compile(project)
    generated = tmp_path / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    mode_id = next(e["id"] for e in plan.id_map if e["node"] == "down" and e["key"] == "mode")
    harness = r'''
#include "orpheus_graph.h"
#include "orpheus_control.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
typedef struct { int32_t value; int calls; } CallbackState;
typedef struct { char text[8]; int calls; } StringState;
static int string_set(void* state, const char* key, const OrpheusValue* value) {
    StringState* s = (StringState*)state;
    if (strcmp(key, "mode") || value->type != ORPHEUS_VALUE_STRING ||
        !value->value.str || strlen(value->value.str) >= sizeof(s->text))
        return ORPHEUS_ERR_INVALID_ARG;
    strcpy(s->text, value->value.str);
    s->calls++;
    return ORPHEUS_OK;
}
static int callback_set(void* state, const char* key, const OrpheusValue* value) {
    CallbackState* s = (CallbackState*)state;
    if ((strcmp(key, "value") && strcmp(key, "mode")) ||
        value->type != ORPHEUS_VALUE_INT || value->value.i32 < 0)
        return ORPHEUS_ERR_INVALID_ARG;
    s->value = 2 * value->value.i32 + 1;
    s->calls++;
    return ORPHEUS_OK;
}
static int binary_has_error(int32_t value) {
    struct { OrpheusMessageHeader header; int32_t value; } request = {
        .header = {.route_id=@MODE_ID@u, .bits=ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, 0, 9, 1)},
        .value=value
    };
    uint32_t response[16] = {0};
    size_t length = 0;
    int rc = orpheus_control_message((const uint8_t*)&request, sizeof(request),
                                    (uint8_t*)response, sizeof(response), &length);
    if (rc != ORPHEUS_OK || length < sizeof(OrpheusMessageHeader)) return -1;
    return (ORPHEUS_MSG_FLAGS((OrpheusMessageHeader*)response) & ORPHEUS_MSG_FLAG_ERROR) != 0;
}
int main(void) {
    OrpheusValue value = {.type=ORPHEUS_VALUE_INT, .value.i32=1}, actual;
    CHECK(orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    CHECK(orpheus_control_set_value("down", "mode", value) == ORPHEUS_ERR_UNSUPPORTED);
    CHECK(orpheus_control_get_value("down", "mode", &actual) == ORPHEUS_OK);
    CHECK(actual.value.i32 == 0);
    CHECK(orpheus_control_set_value_id(@MODE_ID@u, value) == ORPHEUS_ERR_UNSUPPORTED);
    CHECK(binary_has_error(1) == 1);
    CHECK(orpheus_control_get_value("down", "mode", &actual) == ORPHEUS_OK);
    CHECK(actual.value.i32 == 0);
    value.type = ORPHEUS_VALUE_FLOAT;
    value.value.f32 = -6.0f;
    CHECK(orpheus_control_set_value("gain", "gain_db", value) == ORPHEUS_OK);
    CHECK(orpheus_control_get_value("gain", "gain_db", &actual) == ORPHEUS_OK);
    CHECK(actual.value.f32 == -6.0f);
    {
        CallbackState state = {0, 0};
        const OrpheusComponentInterface iface = {.set_parameter=callback_set};
        OrpheusSlotInfo slot = {
            .kind=ORPHEUS_SLOT_SETTING, .key="value", .type=ORPHEUS_VALUE_INT,
            .size=sizeof(int32_t), .count=1, .update_policy=ORPHEUS_UPDATE_IMMEDIATE
        };
        orpheus_control_set_reg_node("callback", &state, &iface);
        orpheus_control_slot_register(&state, &slot);
        value.type = ORPHEUS_VALUE_INT; value.value.i32 = 7;
        CHECK(orpheus_control_set_value("callback", "value", value) == ORPHEUS_OK);
        CHECK(state.value == 15 && state.calls == 1);
        value.value.i32 = -1;
        CHECK(orpheus_control_set_value("callback", "value", value) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(state.value == 15 && state.calls == 1);
        slot.key = "direct"; slot.flags = ORPHEUS_SLOT_DIRECT_WRITE;
        orpheus_control_slot_register(&state, &slot);
        value.value.i32 = 9;
        CHECK(orpheus_control_set_value("callback", "direct", value) == ORPHEUS_OK);
        CHECK(state.value == 9 && state.calls == 1);
        slot.key = "bulk"; slot.kind = ORPHEUS_SLOT_BULK;
        orpheus_control_slot_register(&state, &slot);
        CHECK(orpheus_control_set_value("callback", "bulk", value) != ORPHEUS_OK);
        CHECK(state.value == 9 && state.calls == 1);
        orpheus_control_reset();
        orpheus_control_set_reg_node("down", &state, &iface);
        slot.key = "mode"; slot.kind = ORPHEUS_SLOT_SETTING; slot.flags = 0;
        orpheus_control_slot_register(&state, &slot);
        CHECK(binary_has_error(5) == 0);
        CHECK(state.value == 11 && state.calls == 2);
        CHECK(binary_has_error(-1) == 1);
        CHECK(state.value == 11 && state.calls == 2);
    }
    {
        StringState state = {{0}, 0};
        const OrpheusComponentInterface iface = {.set_parameter=string_set};
        const OrpheusSlotInfo slot = {
            .kind=ORPHEUS_SLOT_SETTING, .key="mode", .type=ORPHEUS_VALUE_STRING,
            .size=1, .count=sizeof(state.text), .update_policy=ORPHEUS_UPDATE_IMMEDIATE
        };
        orpheus_control_reset();
        orpheus_control_set_reg_node("down", &state, &iface);
        orpheus_control_slot_register(&state, &slot);
        value.type = ORPHEUS_VALUE_STRING; value.value.str = "start";
        CHECK(orpheus_control_set_value("down", "mode", value) == ORPHEUS_OK);
        CHECK(strcmp(state.text, "start") == 0 && state.calls == 1);
        CHECK(binary_has_error(0x00006B6F) == 0);
        CHECK(strcmp(state.text, "ok") == 0 && state.calls == 2);
        CHECK(binary_has_error(0x61626364) == 1);
        CHECK(strcmp(state.text, "ok") == 0 && state.calls == 2);
    }
    return 0;
}
'''
    (generated / "src/settings_test.c").write_text(
        harness.replace("@MODE_ID@", str(mode_id)), encoding="utf-8",
    )
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write("\nadd_executable(settings_test src/settings_test.c)\n"
                     "target_link_libraries(settings_test PRIVATE orpheus_graph)\n")
    _build(generated, tmp_path)
    result = subprocess.run([str(generated / "build/settings_test.exe")], cwd=tmp_path,
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
