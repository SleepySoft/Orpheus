"""音频快照同步转计算载荷的完整位型与调用时间验收。"""

import pytest

from frequency_interface_fixtures import BIT_FILL, parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_snapshot_source_algorithm(orpheus_test_snapshot_source_State* s,
    orpheus_test_snapshot_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        int32_t call=s->calls++;
        for(int32_t i=0;i<s->channels*(int32_t)ctx->frame_count;i++) { @FILL@ }
        int rc=orpheus_test_snapshot_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_snapshot_capture_algorithm(orpheus_test_snapshot_capture_State* s,
    orpheus_test_snapshot_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(s->calls>=4 || ctx->trigger_input || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@)
        || io->in.buffer->frame_count!=@FRAMES@) return ORPHEUS_ERR_INVALID_ARG;
    int32_t call=s->calls;
    for(int32_t i=0;i<s->channels*@FRAMES@;i++) { @CHECK@ }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,fixed", [(1, 1, False), (22, 17, True),
                                                (128, 128, True), (3, 7, False)])
def test_audio_bulk_snapshot_four_entries(tmp_path, channels, frames, fixed):
    source = create_component(tmp_path, "snapshot_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": "param:channels", "drive_port_no": 1},
        "channels": parameter(channels, 128), "calls": scalar("int", 0),
    }, SOURCE.replace("@FILL@", BIT_FILL), source=True)
    check = BIT_FILL.replace("memcpy(io->out.samples+i,&value,4);",
                            "uint32_t actual;memcpy(&actual,io->in.samples+i,4);"
                            "if(actual!=value)return ORPHEUS_ERR_PROCESSING;")
    capture = create_component(tmp_path, "snapshot_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "f32",
               "channels": "param:channels", "block_size": frames, "process": True},
        "channels": parameter(channels, 128), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@CHECK@", check).replace("@FRAMES@", str(frames)))
    manifest = ROOT / "components" / "orpheus" / "builtin" / "audio_bulk_snapshot" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.snapshot_source"),
        "snapshot": Node("snapshot", "orpheus.builtin.audio_bulk_snapshot", params={
            "channels": channels, "elements": frames if fixed else 0,
        }),
        "capture": Node("capture", "orpheus.test.snapshot_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("snapshot", "in")),
        Connection(PortRef("snapshot", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
