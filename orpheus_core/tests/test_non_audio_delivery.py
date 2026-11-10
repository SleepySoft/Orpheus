"""数组载荷的元素数与 Task 处理量子相互独立。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import compile_delivery_backends, create_component, scalar


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_data_source_algorithm(orpheus_test_data_source_State* s,
    orpheus_test_data_source_IO* io, const OrpheusProcessContext* ctx) {
    if(ctx->frame_count!=8) return ORPHEUS_ERR_INVALID_ARG;
    io->out.buffer->frame_count=(uint32_t)s->elements;
    for(int32_t i=0;i<s->elements;i++) io->out.samples[i]=(float)(i+1);
    return orpheus_test_data_source_emit_out(ctx,&io->out);
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_data_capture_algorithm(orpheus_test_data_capture_State* s,
    orpheus_test_data_capture_IO* io, const OrpheusProcessContext* ctx) {
    if(ctx->frame_count!=8 || ctx->valid_frames!=8
        || io->in.buffer->frame_count!=(uint32_t)s->elements) return ORPHEUS_ERR_INVALID_ARG;
    for(int32_t i=0;i<s->elements;i++) {
        if(io->in.samples[i]!=(float)(i+1)) return ORPHEUS_ERR_UNSUPPORTED;
        s->total+=io->in.samples[i];
    }
    s->trace=s->trace*10+1;
    s->calls++;
    s->last=io->in.samples[0];
    s->frame=(int32_t)ctx->frame_index;
    s->epoch=(int32_t)ctx->epoch;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("kind", ["bulk", "control"])
@pytest.mark.parametrize("elements", [7, 13])
def test_data_payload_does_not_replace_task_quantum(tmp_path, kind, elements):
    parameter = {**scalar("int", elements), "range": [1, 64],
                 "affects_signature": True, "update_policy": "restart_required"}
    port = {"type": kind, "sample_format": "f32", "channels": 1, "block_size": "param:elements"}
    source = create_component(tmp_path, "data_source", {
        "out": {**port, "direction": "output", "drive_port_no": 1},
        "elements": parameter, "mode": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(tmp_path, "data_capture", {
        "in": {**port, "direction": "input", "process": True},
        "elements": parameter, "trace": scalar("int", 0), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "total": scalar("float", 0.0),
        "frame": scalar("int", 0), "epoch": scalar("int", 0),
    }, CAPTURE)
    registry = Registry()
    for manifest in (source, capture):
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={"source": Node("source", "orpheus.test.data_source"),
               "capture": Node("capture", "orpheus.test.data_capture")},
        connections=[Connection(PortRef("source", "out"), PortRef("capture", "in"))],
    ))
    backends = compile_delivery_backends(tmp_path, registry, project, [source, capture])
    assert all(config["frames"] == 8 for config in backends[2].node_configs.values())
    total = elements * (elements + 1) // 2
    expected = [[0, 0, 1, 1, 1, total, 0, 1], [1, 0, 11, 2, 1, total * 2, 8, 1]]
    for executable in ("dynamic", "generated_backend"):
        result, rows = run_backend(backends, executable)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == expected
