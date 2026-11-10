"""常量表独立出口与 latest 表插值的真实调用、位型和边界验证。"""

import ctypes
import math
import struct

import pytest

from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_bulk_arithmetic_interfaces import HARNESS
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar


def f32(value):
    return ctypes.c_float(value).value


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def four_entry_harness():
    return ("#include <stdlib.h>\n" + HARNESS).replace(
        'runtime.process_task("task_source",8)',
        '(atoi(argv[argc-1]) ? runtime.process_task("task_source",8) : runtime.process_block(8))',
    ).replace("orpheus_generated_process_task_task_source(8)",
              "(atoi(argv[argc-1]) ? orpheus_generated_process_task_task_source(8) : orpheus_generated_process(8))")


def check_four_entries(case, expected):
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            result, rows = run_backend(case, backend, mode=mode)
            assert result.returncode == 0, (backend, mode, result.stdout, result.stderr)
            assert rows == expected


CLOCK = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_constant_clock_algorithm(orpheus_test_constant_clock_State* s,
    orpheus_test_constant_clock_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;
    int rc=orpheus_test_constant_clock_emit_out(ctx,&io->out);
    return rc ? rc : orpheus_test_constant_clock_emit_out(ctx,&io->out);
}
'''

CONSTANT_CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_constant_capture_algorithm(orpheus_test_constant_capture_State* s,
    orpheus_test_constant_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@VALUES@}, order[]={@ORDER@};
    unsigned slot=order[s->calls%@ACTIVE@];
    if(ctx->trigger_input!=slot || ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/(@ACTIVE@*2)*8)) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* inputs[]={io->table.buffer,io->first.buffer,io->end.buffer};
    const OrpheusBuffer* input=inputs[slot];
    unsigned count=slot==0 ? @COUNT@ : 1, start=slot==2 ? @COUNT@-1 : 0;
    if(input->frame_count!=count || memcmp(input->data,expected+start,count*4))
        return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("count,mask", [(1, 7), (3, 7), (3, 6), (2048, 7), (2048, 1)])
def test_constant_each_outlet_is_independent_and_preserves_bits(tmp_path, count, mask):
    values = ([-0.0, f32(2**-149), f32(3.4028234663852886e38)]
              if count == 3 else [f32((i - count // 2) * .03125) for i in range(count)])
    order = [i for i in range(3) if mask & (1 << i)]
    source = create_component(tmp_path, "constant_clock", {
        "out": {"direction": "output", "type": "trigger", "drive_port_no": 1},
    }, CLOCK, source=True)
    port = {"direction": "input", "type": "bulk", "sample_format": "f32",
            "channels": 1, "process": True}
    capture = create_component(tmp_path, "constant_capture", {
        "table": {**port, "block_size": count},
        "first": {**port, "block_size": 1}, "end": {**port, "block_size": 1},
        "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CONSTANT_CAPTURE.replace("@VALUES@", ",".join(str(bits(v)) for v in values))
        .replace("@ORDER@", ",".join(map(str, order))).replace("@ACTIVE@", str(len(order))).replace("@COUNT@", str(count)))
    constant = ROOT / "components" / "orpheus" / "builtin" / "bulk_constant" / "component.yaml"
    manifests = [source, constant, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.constant_clock"),
        "table": Node("table", "orpheus.builtin.bulk_constant",
                      params={"elements": count, "values": ",".join(format(v, ".9g") for v in values)}),
        "capture": Node("capture", "orpheus.test.constant_capture"),
    }, connections=[
        Connection(PortRef("table", outlet), PortRef("capture", inlet))
        for i, (outlet, inlet) in enumerate((("out", "table"), ("first", "first"), ("last", "end")))
        if mask & (1 << i)
    ]), drive_connections=[DriveConnection(PortRef("source", "out"), PortRef("table", "tick"))])
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    check_four_entries(case, [[0, 2 * len(order), 0, 0], [1, 4 * len(order), 0, 8]])


LOOKUP_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
#include <math.h>
int orpheus_test_lookup_source_algorithm(orpheus_test_lookup_source_State* s,
    orpheus_test_lookup_source_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;
    for(int i=0;i<@POINTS@;i++) {
        int j=@DESCENDING@ ? @POINTS@-1-i : i;
        io->axis.samples[i]=(float)(j-@POINTS@/2);
        io->values.samples[i]=(float)((j*37)%199-99)*.03125f;
    }
    float query[]={-INFINITY,-@POINTS@/2.0f-1,-@POINTS@/2.0f,
        -@POINTS@/2.0f+.3f,@POINTS@-1-@POINTS@/2.0f,
        @POINTS@-@POINTS@/2.0f,INFINITY,NAN};
    for(unsigned i=0;i<8;i++) {
        io->x.samples[0]=query[i];
        int rc=orpheus_test_lookup_source_emit_x(ctx,&io->x);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

LOOKUP_CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_lookup_capture_algorithm(orpheus_test_lookup_capture_State* s,
    orpheus_test_lookup_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/8*8)
        || memcmp(io->in.samples,expected+s->calls%8,4)) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=io->in.samples[0];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("points,descending", [(2, False), (2, True), (2048, False), (2048, True)])
def test_lookup_latest_tables_and_eight_queries_per_tick(tmp_path, points, descending):
    def bulk(direction, count, **extra):
        return {"direction": direction, "type": "bulk", "sample_format": "f32",
                "channels": 1, "block_size": count, **extra}
    axis = [i - points // 2 for i in range(points)]
    values = [((i * 37) % 199 - 99) * .03125 for i in range(points)]
    queries = [-math.inf, axis[0] - 1, axis[0], f32(axis[0] + f32(.3)),
               axis[-1], axis[-1] + 1, math.inf, math.nan]
    expected = []
    for x in queries:
        value = 0.0
        if axis[0] <= x <= axis[-1]:
            i = next(i for i in range(points - 1) if axis[i] <= x <= axis[i + 1])
            t = f32(f32(x - axis[i]) / f32(axis[i + 1] - axis[i]))
            value = (values[i] if t == 0 else values[i + 1] if t == 1 else values[i]
                     if values[i] == values[i + 1] else f32(f32(f32(1 - t) * values[i]) + f32(t * values[i + 1])))
        expected.append(value)
    source = create_component(tmp_path, "lookup_source", {
        "axis": bulk("output", points), "values": bulk("output", points),
        "x": bulk("output", 1, drive_port_no=1),
    }, LOOKUP_SOURCE.replace("@POINTS@", str(points)).replace("@DESCENDING@", str(int(descending))), source=True)
    capture = create_component(tmp_path, "lookup_capture", {
        "in": bulk("input", 1, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, LOOKUP_CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected)))
    lookup = ROOT / "components" / "orpheus" / "builtin" / "bulk_lookup" / "component.yaml"
    manifests = [source, lookup, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.lookup_source"),
        "lookup": Node("lookup", "orpheus.builtin.bulk_lookup", params={"points": points}),
        "capture": Node("capture", "orpheus.test.lookup_capture"),
    }, connections=[
        *[Connection(PortRef("source", key), PortRef("lookup", key)) for key in ("axis", "values", "x")],
        Connection(PortRef("lookup", "out"), PortRef("capture", "in")),
    ]))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    check_four_entries(case, [[0, 8, 0, 0], [1, 16, 0, 8]])
