"""音频入口驱动、三个显式出口和无跨调用状态的包络四入口验证。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_envelope_source_algorithm(orpheus_test_envelope_source_State* s,
    orpheus_test_envelope_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned phase=0;phase<2;phase++) {
        int call=s->calls++;
        for(int c=0;c<@CHANNELS@;c++) {
            io->start.samples[c]=(float)(c-call)*.03125f;
            io->target.samples[c]=(float)(call+c+1)*.0625f;
        }
        io->alpha.samples[0]=@ALPHA@;
        io->freeze.samples[0]=call%2 && @FREEZE@;
        io->start.buffer->frame_count=io->target.buffer->frame_count=
            io->alpha.buffer->frame_count=io->freeze.buffer->frame_count=1;
        for(int n=0;n<@FRAMES@;n++) for(int c=0;c<@CHANNELS@;c++)
            io->out.samples[n*@CHANNELS@+c]=(float)(n*17+c+call)*.03125f;
        int rc=orpheus_test_envelope_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_envelope_capture_algorithm(orpheus_test_envelope_capture_State* s,
    orpheus_test_envelope_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@VALUES@},audio[]={@AUDIO@};
    unsigned call=s->calls/3,port=s->calls%3;
    if(ctx->trigger_input!=port || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ ||
        ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(call/2*@FRAMES@)) return ORPHEUS_ERR_PROCESSING;
    const uint32_t* envelope=expected+call*@COUNT@;
    if(port==0 && (io->in.buffer->frame_count!=@FRAMES@ ||
        memcmp(io->in.samples,envelope,@COUNT@*4))) return ORPHEUS_ERR_PROCESSING;
    if(port==1 && (io->gains.buffer->frame_count!=1 ||
        memcmp(io->gains.samples,envelope+@COUNT@-@CHANNELS@,@CHANNELS@*4))) return ORPHEUS_ERR_PROCESSING;
    if(port==2 && (io->audio.buffer->frame_count!=@FRAMES@ ||
        memcmp(io->audio.samples,audio+call*@COUNT@,@COUNT@*4))) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,alpha,freeze", [(1, 1, .25, False), (128, 7, .75, True), (3, 8, -.5, True)])
def test_audio_drives_explicit_envelope_gains_and_passthrough(tmp_path, channels, frames, alpha, freeze):
    expected, passthrough = [], []
    for call in range(4):
        gains = [f32((c - call) * .03125) for c in range(channels)]
        targets = [f32((call + c + 1) * .0625) for c in range(channels)]
        for n in range(frames):
            for c in range(channels):
                if not (call % 2 and freeze):
                    gains[c] = f32(f32(f32(1 - alpha) * targets[c]) + f32(alpha * gains[c]))
                expected.append(bits(gains[c]))
                passthrough.append(bits((n * 17 + c + call) * .03125))
    bulk = {"type": "bulk", "sample_format": "f32", "block_size": 1}
    source = create_component(tmp_path, "envelope_source", {
        "start": {**bulk, "direction": "output", "channels": channels},
        "target": {**bulk, "direction": "output", "channels": channels},
        "alpha": {**bulk, "direction": "output", "channels": 1},
        "freeze": {**bulk, "direction": "output", "channels": 1, "sample_format": "i32"},
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames))
        .replace("@ALPHA@", str(alpha)).replace("@FREEZE@", str(int(freeze))), source=True)
    capture = create_component(tmp_path, "envelope_capture", {
        "in": audio("input", channels=channels, process=True),
        "gains": {**bulk, "direction": "input", "channels": channels, "process": True},
        "audio": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@VALUES@", ",".join(map(str, expected)))
        .replace("@AUDIO@", ",".join(map(str, passthrough))).replace("@FRAMES@", str(frames))
        .replace("@COUNT@", str(channels * frames)).replace("@CHANNELS@", str(channels)))
    envelope = ROOT / "components" / "orpheus" / "builtin" / "one_pole_envelope" / "component.yaml"
    manifests = [source, envelope, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    edges = [Connection(PortRef("source", key), PortRef("envelope", key))
             for key in ("start", "target", "alpha", "freeze")]
    edges.append(Connection(PortRef("source", "out"), PortRef("envelope", "in")))
    edges += [Connection(PortRef("envelope", key), PortRef("capture", dest))
              for key, dest in (("out", "in"), ("gains", "gains"), ("audio", "audio"))]
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.envelope_source"),
        "envelope": Node("envelope", "orpheus.builtin.one_pole_envelope", params={"channels": channels}),
        "capture": Node("capture", "orpheus.test.envelope_capture"),
    }, connections=edges))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 6, 0, 0], [1, 12, 0, frames]])
