"""数组拆并保留原始位型，每次实际交付运行，载荷尺寸不替代 Task 量子。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_raw_channel_source_algorithm(orpheus_test_raw_channel_source_State* s,
    orpheus_test_raw_channel_source_IO* io, const OrpheusProcessContext* ctx) {
    static const uint32_t patterns[]={0x7fc12345,0x7fa23456,0x80000000,0x7f800000,
        0xff800000,0x00000001,0x3f800000};
    if(ctx->frame_count!=8) return ORPHEUS_ERR_INVALID_ARG;
    for(int32_t i=0;i<s->channels*s->elements;i++)
        memcpy((unsigned char*)io->out.buffer->data+(size_t)i*4,&patterns[i%7],4);
    io->out.buffer->frame_count=(uint32_t)s->elements;
    return orpheus_test_raw_channel_source_emit_out(ctx,&io->out);
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_raw_channel_capture_algorithm(orpheus_test_raw_channel_capture_State* s,
    orpheus_test_raw_channel_capture_IO* io, const OrpheusProcessContext* ctx) {
    static const uint32_t patterns[]={0x7fc12345,0x7fa23456,0x80000000,0x7f800000,
        0xff800000,0x00000001,0x3f800000};
    if(ctx->frame_count!=8 || ctx->valid_frames!=8
        || io->in.buffer->frame_count!=(uint32_t)s->elements) return ORPHEUS_ERR_INVALID_ARG;
    int active=0, trigger=-1, present=0;
    for(int32_t ch=0;ch<s->channels;ch++) if(s->mask&(1<<ch)) active++;
    int step=s->calls%active;
    for(int32_t ch=0;ch<s->channels;ch++)
        if((s->mask&(1<<ch)) && step--==0) { trigger=ch; break; }
    for(int32_t ch=0;ch<s->channels;ch++) {
        int available=(s->mask&(1<<ch)) && (ctx->frame_index>0 || ch<=trigger);
        present+=!!available;
        for(int32_t i=0;i<s->elements;i++) {
            uint32_t index=(uint32_t)(i*s->channels+ch), actual;
            uint32_t expected=available ? patterns[index%7] : 0;
            memcpy(&actual,(const unsigned char*)io->in.buffer->data+(size_t)index*4,4);
            if(actual!=expected) return ORPHEUS_ERR_UNSUPPORTED;
            s->total+=1;
        }
    }
    s->trace=s->trace*10+present;
    s->calls++;
    s->last=(float)present;
    s->frame=(int32_t)ctx->frame_index;
    s->epoch=(int32_t)ctx->epoch;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,elements,mask", [(1, 7, 1), (3, 13, 7), (3, 7, 5)])
def test_bulk_routing_preserves_bits_and_per_arrival_calls(tmp_path, channels, elements, mask):
    def parameter(value, maximum):
        return {**scalar("int", value), "range": [1, maximum], "affects_signature": True,
                "update_policy": "restart_required"}
    port = {"type": "bulk", "sample_format": "f32", "channels": "param:channels",
            "block_size": "param:elements"}
    parameters = {"channels": parameter(channels, 128), "elements": parameter(elements, 65536)}
    source = create_component(tmp_path, "raw_channel_source", {
        "out": {**port, "direction": "output", "drive_port_no": 1},
        **parameters, "mode": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(tmp_path, "raw_channel_capture", {
        "in": {**port, "direction": "input", "process": True},
        **parameters, "mask": scalar("int", mask),
        "trace": scalar("int", 0), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "total": scalar("float", 0.0),
        "frame": scalar("int", 0), "epoch": scalar("int", 0),
    }, CAPTURE)
    manifests = [source, capture, *[
        ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
        for name in ("bulk_deinterleave", "bulk_interleave")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    settings = {"channels": channels, "elements": elements}
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={
            "source": Node("source", "orpheus.test.raw_channel_source", params=dict(settings)),
            "split": Node("split", "orpheus.builtin.bulk_deinterleave", params=dict(settings)),
            "join": Node("join", "orpheus.builtin.bulk_interleave", params=dict(settings)),
            "capture": Node("capture", "orpheus.test.raw_channel_capture", params=dict(settings)),
        },
        connections=[
            Connection(PortRef("source", "out"), PortRef("split", "in")),
            *[Connection(PortRef("split", f"out{ch}"), PortRef("join", f"in{ch}"))
              for ch in range(channels) if mask & (1 << ch)],
            Connection(PortRef("join", "out"), PortRef("capture", "in")),
        ],
    ))
    backends = compile_delivery_backends(tmp_path, registry, project, manifests)
    assert all(config["frames"] == 8 for config in backends[2].node_configs.values())
    active = mask.bit_count()
    trace = calls = 0
    expected = []
    for tick in range(2):
        for index in range(active):
            trace = trace * 10 + (index + 1 if tick == 0 else active)
            calls += 1
        expected.append([tick, 0, trace, calls, active, calls * elements * channels, tick * 8, 1])
    for executable in ("dynamic", "generated_backend"):
        result, rows = run_backend(backends, executable)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == expected
