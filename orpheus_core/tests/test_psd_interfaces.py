"""真实音频交付保持逐位直通；功率谱数组及平滑状态由原生契约独立验证。"""

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_psd_source_algorithm(orpheus_test_psd_source_State* s,
    orpheus_test_psd_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        int32_t call=s->calls++;
        for(uint32_t f=0;f<ctx->frame_count;f++) for(int32_t c=0;c<s->channels;c++)
            io->out.samples[f*s->channels+c]=f==0 ? (call+1)*(c+1)*.03125f : 0;
        int rc=orpheus_test_psd_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
int orpheus_test_psd_capture_algorithm(orpheus_test_psd_capture_State* s,
    orpheus_test_psd_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(s->calls>=4 || ctx->trigger_input || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@))
        return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned f=0;f<@FRAMES@;f++) for(int32_t c=0;c<s->channels;c++) {
        float expected=f==0 ? (s->calls+1)*(c+1)*.03125f : 0;
        if(io->in.samples[f*s->channels+c]!=expected) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,smoothing", [
    (1, 4, 1), (3, 128, 4), (32, 4096, 128), (2, 6, 8), (1, 2, 1),
])
def test_psd_four_entries(tmp_path, channels, frames, smoothing):
    source = create_component(tmp_path, "psd_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": "param:channels", "drive_port_no": 1},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(tmp_path, "psd_capture", {
        "in": {"direction": "input", "type": "audio", "sample_format": "f32",
               "channels": "param:channels", "process": True},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@FRAMES@", str(frames)))
    manifest = ROOT / "components" / "orpheus" / "builtin" / "psd" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.psd_source"),
        "psd": Node("psd", "orpheus.builtin.psd", params={"channels": channels, "smoothing": smoothing}),
        "capture": Node("capture", "orpheus.test.psd_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("psd", "in")),
        Connection(PortRef("psd", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert all(config["frames"] == frames for config in case[2].node_configs.values())
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
