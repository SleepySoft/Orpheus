"""写入不续调、读取由显式触发决定；大块与小块交付时间独立验证。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, four_entry_harness


@pytest.mark.parametrize("channels,frames,factor", [(1, 1, 1), (3, 7, 4), (64, 3, 13)])
def test_split_publication_never_drives_reads(tmp_path, channels, frames, factor):
    input_frames = frames * factor
    expected = [(((n * 17 + c * 31) % 103) - 51) * .03125
                for n in range(2 * input_frames) for c in range(channels)]
    for packet in range(2):
        expected[packet * input_frames * channels] = -0.0
    source = create_component(tmp_path, "split_source", {
        "batch": audio("output", channels=channels, block_size=input_frames, drive_port_no=1),
        "tick": {"direction": "output", "type": "trigger", "drive_port_no": 2},
        "calls": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_split_source_algorithm(orpheus_test_split_source_State* s,
    orpheus_test_split_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned n=0;n<@INPUT@;n++) for(unsigned c=0;c<@CHANNELS@;c++)
        io->batch.samples[n*@CHANNELS@+c]=
            (float)((int)(((s->calls*@INPUT@+n)*17+c*31)%103)-51)*.03125f;
    io->batch.samples[0]=-0.0f;
    io->batch.delivery.frame_index=(uint64_t)s->calls*@INPUT@;
    int rc=orpheus_test_split_source_emit_batch(ctx,&io->batch);
    if(rc) return rc;
    for(unsigned part=0;part<@FACTOR@;part++) {
        rc=orpheus_test_split_source_emit_tick(ctx,&io->tick);
        if(rc) return rc;
    }
    s->calls++;
    return ORPHEUS_OK;
}
'''.replace("@INPUT@", str(input_frames)).replace("@CHANNELS@", str(channels)).replace("@FACTOR@", str(factor)),
        source=True)
    capture = create_component(tmp_path, "split_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_split_capture_algorithm(orpheus_test_split_capture_State* s,
    orpheus_test_split_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(s->calls>=2*@FACTOR@ || ctx->trigger_input!=0 || ctx->frame_count!=@FRAMES@ ||
       ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
       ctx->frame_index!=(uint64_t)s->calls*@FRAMES@ ||
       io->in.buffer->frame_count!=@FRAMES@ ||
       memcmp(io->in.samples,expected+s->calls*@COUNT@,@COUNT@*4))
        return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=io->in.samples[@COUNT@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''.replace("@EXPECTED@", ",".join(str(bits(value)) for value in expected))
        .replace("@FACTOR@", str(factor)).replace("@FRAMES@", str(frames)).replace("@COUNT@", str(frames * channels)))
    split = ROOT / "components" / "orpheus" / "builtin" / "block_split" / "component.yaml"
    manifests = [source, split, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.split_source"),
        "split": Node("split", "orpheus.builtin.block_split", params={
            "channels": channels, "factor": factor, "output_frames": frames}),
        "capture": Node("capture", "orpheus.test.split_capture"),
    }, connections=[
        Connection(PortRef("source", "batch"), PortRef("split", "in")),
        Connection(PortRef("source", "tick"), PortRef("split", "tick")),
        Connection(PortRef("split", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert case[2].node_configs["split"]["frames"] == frames
    assert case[2].node_configs["split"]["port_signatures"]["in"]["frames"] == input_frames
    check_four_entries(case, [
        [0, factor, expected[input_frames * channels - 1], (factor - 1) * frames],
        [1, 2 * factor, expected[-1], (2 * factor - 1) * frames],
    ])
