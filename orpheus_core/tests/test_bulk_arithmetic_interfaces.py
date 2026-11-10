"""数组相减的两个真实调用点分别验证当前值和上一出口尚未更新的数据。"""

import ctypes

import pytest

from array_interface_fixtures import register_array_interfaces
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar


CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_arithmetic_capture_algorithm(orpheus_test_arithmetic_capture_State* s,
    orpheus_test_arithmetic_capture_IO* io,const OrpheusProcessContext* ctx) {
    int tick=s->calls/2,second=s->calls%2;
    if(ctx->frame_count!=8 || ctx->valid_frames!=8
        || ctx->frame_index!=(uint64_t)(tick*8) || ctx->epoch!=1)
        return ORPHEUS_ERR_UNSUPPORTED;
    for(int i=0;i<s->channels*s->elements;i++) {
        float a=((i*17+tick*13)%129-64)*0.03125f;
        float b=(second || tick) ? ((i*11+(second ? tick : tick-1)*7)%97-48)*0.0625f : 0;
        if(io->in.samples[i]!=a-b) return ORPHEUS_ERR_UNSUPPORTED;
    }
    s->last=io->in.samples[s->channels*s->elements-1];
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''

HARNESS = r'''
#include <stdio.h>
#ifdef DYNAMIC
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime runtime;
static double get(const char* key) {
    OrpheusValue v={};if(runtime.get_parameter("capture",key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? v.value.f32 : v.value.i32;
}
#else
#include "orpheus_graph.h"
#include "orpheus_control.h"
static double get(const char* key) {
    OrpheusValue v={0};if(orpheus_control_get_value("capture",key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? v.value.f32 : v.value.i32;
}
#endif
int main(int argc,char** argv) {
#ifdef DYNAMIC
    if(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])) return 2;
#else
    if(orpheus_generated_init(48000,8)) return 2;
#endif
    for(int tick=0;tick<2;tick++) {
#ifdef DYNAMIC
        if(runtime.process_task("task_source",8)) return 3;
#else
        if(orpheus_generated_process_task_task_source(8)) return 3;
#endif
        printf("%d %.17g %.17g %.17g\n",tick,get("calls"),get("last"),get("frame"));
    }
    return 0;
}
'''


@pytest.mark.parametrize("channels,elements", [(1, 7), (64, 13)])
def test_subtract_each_arrival_has_independent_numeric_oracle(tmp_path, channels, elements):
    registry = Registry()
    register_array_interfaces(tmp_path, registry, paired_source=True)
    capture = create_component(tmp_path, "arithmetic_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "f32",
               "channels": channels, "block_size": elements, "process": True},
        "channels": scalar("int", channels), "elements": scalar("int", elements),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE)
    subtract = ROOT / "components" / "orpheus" / "builtin" / "bulk_subtract" / "component.yaml"
    registry.add_manifest(capture)
    registry.add_manifest(subtract)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={
            "source": Node("source", "orpheus.test.array_source",
                           params={"channels": channels, "elements": elements}),
            "subtract": Node("subtract", "orpheus.builtin.bulk_subtract",
                             params={"channels": channels, "elements": elements}),
            "capture": Node("capture", "orpheus.test.arithmetic_capture"),
        },
        connections=[
            Connection(PortRef("source", "out"), PortRef("subtract", "a")),
            Connection(PortRef("source", "other"), PortRef("subtract", "b")),
            Connection(PortRef("subtract", "out"), PortRef("capture", "in")),
        ],
    ))
    case = compile_delivery_backends(tmp_path, registry, project, [
        tmp_path / "array_source" / "component.yaml", capture, subtract,
    ], HARNESS)
    index = channels * elements - 1
    expected = []
    for tick in range(2):
        a = ((index * 17 + tick * 13) % 129 - 64) * 0.03125
        b = ((index * 11 + tick * 7) % 97 - 48) * 0.0625
        expected.append([tick, (tick + 1) * 2, ctypes.c_float(a - b).value, tick * 8])
    for backend in ("dynamic", "generated_backend"):
        result, rows = run_backend(case, backend)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == expected
