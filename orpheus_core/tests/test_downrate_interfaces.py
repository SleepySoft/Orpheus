"""写满不交付；显式读取组块，并按独立触发拆回原始样本序列。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, four_entry_harness


@pytest.mark.parametrize("channels,frames,factor,roundtrip", [
    (1, 1, 1, False), (3, 7, 4, False), (32, 3, 13, True),
])
def test_group_and_split_are_driven_only_by_explicit_reads(tmp_path, channels, frames, factor, roundtrip):
    total = frames * factor
    expected = [(((n * 17 + c * 31) % 103) - 51) * .03125
                for n in range(2 * total) for c in range(channels)]
    for packet in range(2):
        for part in range(factor):
            expected[(packet * total + part * frames) * channels] = -0.0
    source = create_component(tmp_path, "group_source", {
        "piece": audio("output", channels=channels, block_size=frames, drive_port_no=1),
        "read_batch": {"direction": "output", "type": "trigger", "drive_port_no": 2},
        "read_piece": {"direction": "output", "type": "trigger", "drive_port_no": 3},
        "calls": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_group_source_algorithm(orpheus_test_group_source_State* s,
    orpheus_test_group_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned part=0;part<@FACTOR@;part++) {
        for(unsigned n=0;n<@FRAMES@;n++) for(unsigned c=0;c<@CHANNELS@;c++)
            io->piece.samples[n*@CHANNELS@+c]=
                (float)((int)(((s->calls*@TOTAL@+part*@FRAMES@+n)*17+c*31)%103)-51)*.03125f;
        io->piece.samples[0]=-0.0f;
        io->piece.delivery.frame_index=(uint64_t)s->calls*@TOTAL@+part*@FRAMES@;
        io->piece.delivery.timeline_flags=ctx->timeline_flags;
        if(part) io->piece.delivery.timeline_flags&=~ORPHEUS_TIMELINE_DISCONTINUITY;
        int rc=orpheus_test_group_source_emit_piece(ctx,&io->piece);
        if(rc) return rc;
    }
    int rc=orpheus_test_group_source_emit_read_batch(ctx,&io->read_batch);
    if(rc) return rc;
    if(@ROUNDTRIP@) for(unsigned part=0;part<@FACTOR@;part++) {
        rc=orpheus_test_group_source_emit_read_piece(ctx,&io->read_piece);
        if(rc) return rc;
    }
    s->calls++;
    return ORPHEUS_OK;
}
'''.replace("@FACTOR@", str(factor)).replace("@FRAMES@", str(frames)).replace("@CHANNELS@", str(channels))
        .replace("@TOTAL@", str(total)).replace("@ROUNDTRIP@", str(int(roundtrip))), source=True)
    output_frames = frames if roundtrip else total
    output_calls = factor if roundtrip else 1
    count = output_frames * channels
    capture = create_component(tmp_path, "group_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_group_capture_algorithm(orpheus_test_group_capture_State* s,
    orpheus_test_group_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(s->calls>=2*@CALLS@ || ctx->trigger_input!=0 || ctx->frame_count!=@FRAMES@ ||
       ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
       ctx->frame_index!=(uint64_t)s->calls*@FRAMES@ ||
       io->in.buffer->frame_count!=@FRAMES@ ||
       memcmp(io->in.samples,expected+s->calls*@COUNT@,@COUNT@*4))
        return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=io->in.samples[@COUNT@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''.replace("@EXPECTED@", ",".join(str(bits(value)) for value in expected)).replace("@CALLS@", str(output_calls))
        .replace("@FRAMES@", str(output_frames)).replace("@COUNT@", str(count)))
    downrate = ROOT / "components" / "orpheus" / "builtin" / "downrate" / "component.yaml"
    manifests = [source, downrate, capture]
    nodes = {
        "source": Node("source", "orpheus.test.group_source"),
        "group": Node("group", "orpheus.builtin.downrate", params={
            "channels": channels, "factor": factor, "input_frames": frames}),
        "capture": Node("capture", "orpheus.test.group_capture"),
    }
    connections = [
        Connection(PortRef("source", "piece"), PortRef("group", "in")),
        Connection(PortRef("source", "read_batch"), PortRef("group", "tick")),
    ]
    if roundtrip:
        split = ROOT / "components" / "orpheus" / "builtin" / "block_split" / "component.yaml"
        manifests.append(split)
        nodes["split"] = Node("split", "orpheus.builtin.block_split", params={
            "channels": channels, "factor": factor, "output_frames": frames})
        connections.extend([
            Connection(PortRef("group", "out"), PortRef("split", "in")),
            Connection(PortRef("source", "read_piece"), PortRef("split", "tick")),
            Connection(PortRef("split", "out"), PortRef("capture", "in")),
        ])
    else:
        connections.append(Connection(PortRef("group", "out"), PortRef("capture", "in")))
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=total, graph=Graph(nodes=nodes, connections=connections))
    harness = four_entry_harness().replace(",8)", f",{total})").replace("(8)", f"({total})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert case[2].node_configs["group"]["period"] == 1
    assert case[2].node_configs["group"]["frames"] == total
    assert case[2].node_configs["group"]["port_signatures"]["in"]["frames"] == frames
    assert case[2].node_configs["group"]["port_signatures"]["out"]["frames"] == total
    check_four_entries(case, [
        [0, output_calls, expected[total * channels - 1], (output_calls - 1) * output_frames],
        [1, 2 * output_calls, expected[-1], (2 * output_calls - 1) * output_frames],
    ])


def test_small_task_publishes_once_and_reads_on_explicit_fourth_trigger(tmp_path):
    channels, frames, factor = 3, 7, 4
    total = frames * factor
    expected = [(((n * 17 + c * 31) % 103) - 51) * .03125
                for n in range(2 * total) for c in range(channels)]
    source = create_component(tmp_path, "slow_group_source", {
        "piece": audio("output", channels=channels, block_size=frames, drive_port_no=1),
        "read_batch": {"direction": "output", "type": "trigger", "drive_port_no": 2},
        "calls": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_slow_group_source_algorithm(orpheus_test_slow_group_source_State* s,
    orpheus_test_slow_group_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned n=0;n<7;n++) for(unsigned c=0;c<3;c++)
        io->piece.samples[n*3+c]=(float)((int)(((s->calls*7+n)*17+c*31)%103)-51)*.03125f;
    int rc=orpheus_test_slow_group_source_emit_piece(ctx,&io->piece);
    if(rc) return rc;
    s->calls++;
    return s->calls%4 ? ORPHEUS_OK : orpheus_test_slow_group_source_emit_read_batch(ctx,&io->read_batch);
}
''', source=True)
    capture = create_component(tmp_path, "slow_group_capture", {
        "in": audio("input", channels=channels, block_size=total, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("delay_capture", "slow_group_capture").replace("s->calls/2", "s->calls")
        .replace("ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@",
                 f"ctx->frame_count!={frames} || ctx->valid_frames!={frames} || "
                 "io->in.buffer->frame_count!=@FRAMES@")
        .replace("@EXPECTED@", ",".join(str(bits(value)) for value in expected))
        .replace("@FRAMES@", str(total)).replace("@COUNT@", str(total * channels)))
    downrate = ROOT / "components" / "orpheus" / "builtin" / "downrate" / "component.yaml"
    manifests = [source, downrate, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.slow_group_source"),
        "group": Node("group", "orpheus.builtin.downrate", params={
            "channels": channels, "input_frames": frames, "factor": factor}),
        "capture": Node("capture", "orpheus.test.slow_group_capture"),
    }, connections=[
        Connection(PortRef("source", "piece"), PortRef("group", "in")),
        Connection(PortRef("source", "read_batch"), PortRef("group", "tick")),
        Connection(PortRef("group", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", ",7)").replace("(8)", "(7)").replace("tick<2", "tick<8")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert case[2].node_configs["group"]["frames"] == frames
    assert case[2].node_configs["capture"]["frames"] == frames
    rows = []
    for tick in range(8):
        calls = (tick + 1) // factor
        rows.append([tick, calls, expected[calls * total * channels - 1] if calls else 0,
                     (calls - 1) * total if calls else 0])
    check_four_entries(case, rows)
