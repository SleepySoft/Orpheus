"""整数抽取独立读写、跨调用分组及本地速率时间的四入口数值验收。"""
import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("channels,frames,output_frames,factor,mode,phase", [
    (1, 1, 1, 2, 0, 0), (3, 7, 14, 3, 0, 0), (32, 3, 3, 13, 1, 12),
])
def test_decimation_waits_for_explicit_read_and_converts_delivery_time(
    tmp_path, channels, frames, output_frames, factor, mode, phase,
):
    input_total = output_frames * factor
    publications = input_total // frames
    samples = [f32((((n * 17 + c * 31) % 103) - 51) * .03125)
               for n in range(2 * input_total) for c in range(channels)]
    for start in range(0, 2 * input_total, frames):
        samples[start * channels] = -0.0
    expected = []
    for n in range(2 * output_frames):
        for c in range(channels):
            if mode:
                value = samples[(n * factor + phase) * channels + c]
            else:
                value = 0.0
                for phase_index in range(factor):
                    value = f32(value + samples[(n * factor + phase_index) * channels + c])
                value = f32(value / factor)
            expected.append(value)
    source = create_component(tmp_path, "decimate_source", {
        "piece": audio("output", channels=channels, block_size=frames, drive_port_no=1),
        "read": {"direction": "output", "type": "trigger", "drive_port_no": 2},
        "calls": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_decimate_source_algorithm(orpheus_test_decimate_source_State* s,
    orpheus_test_decimate_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned f=0;f<@FRAMES@;++f) for(unsigned c=0;c<@CHANNELS@;++c)
        io->piece.samples[f*@CHANNELS@+c]=
            (float)((int)(((s->calls*@FRAMES@+f)*17+c*31)%103)-51)*.03125f;
    io->piece.samples[0]=-0.0f;
    int rc=orpheus_test_decimate_source_emit_piece(ctx,&io->piece);
    if(rc) return rc;
    ++s->calls;
    return s->calls%@PUBLICATIONS@ ? ORPHEUS_OK : orpheus_test_decimate_source_emit_read(ctx,&io->read);
}
'''.replace("@FRAMES@", str(frames)).replace("@CHANNELS@", str(channels))
        .replace("@PUBLICATIONS@", str(publications)), source=True)
    capture = create_component(tmp_path, "decimate_capture", {
        "in": audio("input", channels=channels, block_size=output_frames, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0), "frame": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_decimate_capture_algorithm(orpheus_test_decimate_capture_State* s,
    orpheus_test_decimate_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(s->calls>=2 || ctx->trigger_input!=0 || ctx->frame_count!=@FRAMES@ ||
       ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 || ctx->sample_rate!=48000.0/@FACTOR@ ||
       ctx->frame_index!=(uint64_t)s->calls*@OUTPUT@ ||
       ctx->timeline_flags!=(s->calls ? 0u : ORPHEUS_TIMELINE_DISCONTINUITY) ||
       io->in.buffer->frame_count!=@OUTPUT@ ||
       memcmp(io->in.samples,expected+s->calls*@COUNT@,@COUNT@*4))
        return ORPHEUS_ERR_PROCESSING;
    ++s->calls;s->last=io->in.samples[@COUNT@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''.replace("@EXPECTED@", ",".join(str(bits(value)) for value in expected))
        .replace("@FRAMES@", str(frames)).replace("@FACTOR@", str(factor))
        .replace("@OUTPUT@", str(output_frames)).replace("@COUNT@", str(output_frames * channels)))
    resample = ROOT / "components" / "orpheus" / "builtin" / "resample" / "component.yaml"
    manifests = [source, resample, capture]
    registry = Registry()
    for path in manifests:
        registry.add_manifest(path)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.decimate_source"),
        "resample": Node("resample", "orpheus.builtin.resample", params={
            "channels": channels, "input_frames": frames, "output_frames": output_frames,
            "factor": factor, "mode": mode, "phase": phase}),
        "capture": Node("capture", "orpheus.test.decimate_capture"),
    }, connections=[
        Connection(PortRef("source", "piece"), PortRef("resample", "in")),
        Connection(PortRef("source", "read"), PortRef("resample", "tick")),
        Connection(PortRef("resample", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("tick<2", f"tick<{2 * publications}")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    config = case[2].node_configs["resample"]
    assert config["period"] == 1
    assert config["frames"] == frames
    assert config["timeline_from_delivery"] is True
    assert config["port_signatures"]["out"]["sample_rate"] == pytest.approx(48000 / factor)
    rows = []
    for tick in range(2 * publications):
        calls = (tick + 1) // publications
        rows.append([tick, calls, expected[calls * output_frames * channels - 1] if calls else 0,
                     (calls - 1) * output_frames if calls else 0])
    check_four_entries(case, rows)
