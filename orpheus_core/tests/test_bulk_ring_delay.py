"""写完整批后读取环形池，逐位验收实际交付与两圈以上历史。"""

import struct

import pytest

from frequency_interface_fixtures import BIT_FILL, parameter, spectral_bits
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_ring_source_algorithm(orpheus_test_ring_source_State* s,
    orpheus_test_ring_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<@REPEATS@;repeat++) {
        int32_t call=s->calls++;
        for(int32_t i=0;i<s->channels*s->bins*s->batches;i++) {
            @FILL@
        }
        int rc=orpheus_test_ring_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_ring_capture_algorithm(orpheus_test_ring_capture_State* s,
    orpheus_test_ring_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[@CALLS@][@COUNT@]={@EXPECTED@};
    if(s->calls>=@CALLS@ || ctx->trigger_input || ctx->frame_count!=@STEP@ || ctx->valid_frames!=@STEP@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/@REPEATS@*@STEP@))
        return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned i=0;i<@COUNT@;i++) {
        uint32_t actual;
        memcpy(&actual,io->in.samples+i,4);
        if(actual!=expected[s->calls][i]) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def verify_bulk_ring_delay(tmp_path, *, channels=1, bins=129, batches=1, capacity=2193,
                           delay=1161, step=128, reference=None):
    count = channels * bins * batches
    repeats = max(2, (capacity + bins * batches - 1) // (bins * batches))
    source = create_component(tmp_path, "ring_source", {
        "out": {"direction": "output", "type": "bulk", "sample_format": "f32",
                "channels": "param:channels", "block_size": "param:bins*param:batches", "drive_port_no": 1},
        "channels": parameter(channels, 64), "bins": parameter(bins, 65536),
        "batches": parameter(batches, 64), "calls": scalar("int", 0),
    }, SOURCE.replace("@REPEATS@", str(repeats)).replace("@FILL@", BIT_FILL), source=True)
    history, cursor, expected = [0] * (capacity * channels), 0, []
    for call in range(repeats * 2):
        samples = [spectral_bits(i, call) for i in range(count)]
        row = []
        for h in range(batches):
            for k in range(bins):
                for c in range(channels):
                    history[((cursor + k) % capacity) * channels + c] = samples[(h * bins + k) * channels + c]
            for k in range(bins):
                for c in range(channels):
                    row.append(history[((cursor - delay + k) % capacity) * channels + c])
            cursor = (cursor + bins) % capacity
        if reference is not None:
            assert struct.pack(f"<{count}I", *row) == reference(struct.pack(f"<{count}I", *samples))
        expected.append(row)
    assert any(any(row) for row in expected), "必须实际读出历史，不能只比较冷启动全零。"
    code = CAPTURE
    for key, value in {
        "CALLS": repeats * 2, "COUNT": count, "STEP": step, "REPEATS": repeats,
        "EXPECTED": ",".join("{" + ",".join(map(str, row)) + "}" for row in expected),
    }.items():
        code = code.replace(f"@{key}@", str(value))
    capture = create_component(tmp_path, "ring_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "f32",
               "channels": channels, "block_size": bins * batches, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code)
    manifest = ROOT / "components" / "orpheus" / "builtin" / "bulk_ring_delay" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=step, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.ring_source"),
        "delay": Node("delay", "orpheus.builtin.bulk_ring_delay", params={
            "channels": channels, "bins": bins, "batches": batches,
            "capacity_elements": capacity, "delay_elements": delay}),
        "capture": Node("capture", "orpheus.test.ring_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("delay", "in")),
        Connection(PortRef("delay", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{step})").replace("(8)", f"({step})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert all(config["frames"] == step for config in case[2].node_configs.values())
    check_four_entries(case, [[0, repeats, 0, 0], [1, repeats * 2, 0, step]])


@pytest.mark.parametrize("channels,bins,batches,capacity,delay,step", [
    (1, 129, 1, 2193, 1161, 128), (1, 129, 3, 774, 258, 384),
    (3, 7, 2, 11, 9, 32), (2, 7, 2, 11, 0, 32), (2, 7, 2, 11, 34, 32),
    (64, 1, 64, 11, 2147483647, 8), (1, 1, 1, 1, 2147483647, 8),
])
def test_bulk_ring_delay_four_entries(tmp_path, channels, bins, batches, capacity, delay, step):
    verify_bulk_ring_delay(tmp_path, channels=channels, bins=bins, batches=batches,
                           capacity=capacity, delay=delay, step=step)
