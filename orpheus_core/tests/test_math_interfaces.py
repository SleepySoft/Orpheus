"""基础运算的逐入口交付与显式常量触发，对照独立 f32 位型预期。"""

import math

import pytest

from array_interface_fixtures import register_array_interfaces
from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import CLOCK, bits, check_four_entries, f32, four_entry_harness


def divide(a, b):
    if b:
        return f32(a / b)
    if not a:
        return math.nan
    return math.copysign(math.inf, math.copysign(1, a) * math.copysign(1, b))


def scalar_value(operation, a, b, c):
    if operation == 0:
        return min(a, b)
    if operation == 1:
        return max(a, b)
    if operation == 2:
        return float(a > b)
    if operation == 3:
        return float(a != b)
    if operation == 4:
        return b if a != 0 else c
    if operation == 5:
        return a
    if operation == 6:
        return f32(a + b)
    if operation == 7:
        return f32(a - b)
    if operation == 8:
        return f32(a * b)
    if operation == 9:
        return divide(a, b)
    if operation == 10:
        return abs(a)
    return f32(math.log10(a)) if a > 0 else -math.inf if a == 0 else math.nan


SCALAR_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_math_source_algorithm(orpheus_test_math_source_State* s,
    orpheus_test_math_source_IO* io,const OrpheusProcessContext* ctx) {
    io->a.samples[0]=s->ticks ? 10 : 1;
    int rc=orpheus_test_math_source_emit_a(ctx,&io->a);
    if(rc) return rc;
    io->b.samples[0]=(float)(-2-2*s->ticks);
    rc=orpheus_test_math_source_emit_b(ctx,&io->b);
    if(rc) return rc;
    io->c.samples[0]=(float)(-7-2*s->ticks);
    rc=orpheus_test_math_source_emit_c(ctx,&io->c);
    s->ticks++;
    return rc;
}
'''

SCALAR_CAPTURE = r'''
#include "component_state.h"
#include <math.h>
#include <string.h>
int orpheus_test_math_capture_algorithm(orpheus_test_math_capture_State* s,
    orpheus_test_math_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    uint32_t actual;
    if(s->calls>=@CALLS@ || ctx->trigger_input!=(uint32_t)(s->calls%12)
        || ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/@PER_TICK@*8)) return ORPHEUS_ERR_INVALID_ARG;
    float sample=io->value[ctx->trigger_input].samples[0];
    memcpy(&actual,&sample,4);
    uint32_t reference=expected[s->calls];
    if((reference&0x7fffffff)>0x7f800000 ? !isnan(sample) : actual!=reference)
        return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("fallback", [False, True])
def test_all_scalar_operations_each_arrival_and_tick_fallback(tmp_path, fallback):
    port = {"type": "bulk", "sample_format": "f32", "channels": 1, "block_size": 1}
    expected, latest = [], [0.0, 0.0, 0.0]
    for tick in range(2):
        for phase in range(2 if fallback else 3):
            if fallback:
                latest = [-3.0, 2.0, -7.0]
            else:
                latest[phase] = [1.0 if tick == 0 else 10.0, -2.0 - 2 * tick, -7.0 - 2 * tick][phase]
            expected.extend(scalar_value(operation, *latest) for operation in range(12))
    source_name = "constant_clock" if fallback else "math_source"
    source = create_component(tmp_path, source_name, (
        {"out": {"direction": "output", "type": "trigger", "drive_port_no": 1}}
        if fallback else {**{
            key: {**port, "direction": "output", "drive_port_no": i + 1}
            for i, key in enumerate(("a", "b", "c"))}, "ticks": scalar("int", 0)}
    ), CLOCK if fallback else SCALAR_SOURCE, source=True)
    per_tick = 24 if fallback else 36
    capture = create_component(tmp_path, "math_capture", {
        "value": {**port, "direction": "input", "count": 12, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, SCALAR_CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@CALLS@", str(len(expected))).replace("@PER_TICK@", str(per_tick)))
    math_manifest = ROOT / "components" / "orpheus" / "builtin" / "scalar_math" / "component.yaml"
    manifests = [source, math_manifest, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    nodes = {"source": Node("source", "orpheus.test." + source_name),
             "capture": Node("capture", "orpheus.test.math_capture")}
    edges, drives = [], []
    for operation in range(12):
        name = f"op{operation}"
        nodes[name] = Node(name, "orpheus.builtin.scalar_math", params={
            "operation": operation, "a_default": -3.0, "b_default": 2.0, "c_default": -7.0,
        })
        if fallback:
            drives.append(DriveConnection(PortRef("source", "out"), PortRef(name, "tick")))
        else:
            edges.extend(Connection(PortRef("source", key), PortRef(name, key)) for key in ("a", "b", "c"))
        edges.append(Connection(PortRef(name, "out"), PortRef("capture", f"value{operation}")))
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes=nodes, connections=edges),
                      drive_connections=drives)
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    check_four_entries(case, [[0, per_tick, 0, 0], [1, per_tick * 2, 0, 8]])


def bulk_value(operation, a, b):
    if operation == 0:
        return f32(a + b)
    if operation == 1:
        return f32(a - b)
    if operation == 2:
        return f32(a * b)
    if operation == 3:
        return divide(a, b)
    x, y = abs(a), abs(b)
    if x == y:
        return f32(x * f32(1.41421354))
    low, high = min(x, y), max(x, y)
    ratio = f32(low / high)
    return f32(f32(math.sqrt(f32(f32(ratio * ratio) + 1))) * high)


BULK_CAPTURE = r'''
#include "component_state.h"
#include <math.h>
#include <string.h>
int orpheus_test_bulk_math_capture_algorithm(orpheus_test_bulk_math_capture_State* s,
    orpheus_test_bulk_math_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[4][@COUNT@]={@EXPECTED@};
    if(s->calls>=4 || ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/2*8)) return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned i=0;i<@COUNT@;i++) {
        uint32_t actual,reference=expected[s->calls][i];
        memcpy(&actual,io->in.samples+i,4);
        if((reference&0x7fffffff)>0x7f800000 ? !isnan(io->in.samples[i]) : actual!=reference)
            return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("operation", range(5))
@pytest.mark.parametrize("channels,elements", [(1, 7), (128, 13)])
def test_bulk_math_two_arrivals_match_independent_f32_oracle(tmp_path, operation, channels, elements):
    expected = []
    for tick in range(2):
        for second in range(2):
            values = []
            for i in range(channels * elements):
                a = ((i * 17 + tick * 13) % 129 - 64) * .03125
                b = (((i * 11 + (tick if second else tick - 1) * 7) % 97 - 48) * .0625
                     if tick or second else 0.0)
                values.append(bits(bulk_value(operation, a, b)))
            expected.append(values)
    registry = Registry()
    register_array_interfaces(tmp_path, registry, paired_source=True)
    capture = create_component(tmp_path, "bulk_math_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "f32",
               "channels": channels, "block_size": elements, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, BULK_CAPTURE.replace("@COUNT@", str(channels * elements)).replace(
        "@EXPECTED@", ",".join("{" + ",".join(map(str, row)) + "}" for row in expected)))
    math_manifest = ROOT / "components" / "orpheus" / "builtin" / "bulk_math" / "component.yaml"
    registry.add_manifest(math_manifest)
    registry.add_manifest(capture)
    manifests = [tmp_path / "array_source" / "component.yaml", math_manifest, capture]
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.array_source", params={"channels": channels, "elements": elements}),
        "math": Node("math", "orpheus.builtin.bulk_math",
                     params={"channels": channels, "elements": elements, "operation": operation}),
        "capture": Node("capture", "orpheus.test.bulk_math_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("math", "a")),
        Connection(PortRef("source", "other"), PortRef("math", "b")),
        Connection(PortRef("math", "out"), PortRef("capture", "in")),
    ]))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, 8]])


def test_bulk_math_tick_without_data_inputs(tmp_path):
    channels, elements = 2, 7
    clock = create_component(tmp_path, "constant_clock", {
        "out": {"direction": "output", "type": "trigger", "drive_port_no": 1},
    }, CLOCK, source=True)
    expected = "{" + ",".join([str(bits(5.0))] * (channels * elements)) + "}"
    capture = create_component(tmp_path, "bulk_math_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "f32",
               "channels": channels, "block_size": elements, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, BULK_CAPTURE.replace("@COUNT@", str(channels * elements)).replace(
        "@EXPECTED@", ",".join([expected] * 4)))
    math_manifest = ROOT / "components" / "orpheus" / "builtin" / "bulk_math" / "component.yaml"
    manifests = [clock, math_manifest, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.constant_clock"),
        "math": Node("math", "orpheus.builtin.bulk_math", params={
            "channels": channels, "elements": elements, "operation": 4,
            "a_default": 3.0, "b_default": 4.0,
        }),
        "capture": Node("capture", "orpheus.test.bulk_math_capture"),
    }, connections=[Connection(PortRef("math", "out"), PortRef("capture", "in"))]),
        drive_connections=[DriveConnection(PortRef("source", "out"), PortRef("math", "tick"))])
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, 8]])
