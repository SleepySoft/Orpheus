"""双驱动混音每次到达立即交付，不引入会合或同拍去重。"""

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_noise_slew_interfaces import CAPTURE
from test_spectral_reduce_interfaces import f32
from test_table_interfaces import check_four_entries, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_mixer_source_algorithm(orpheus_test_mixer_source_State* s,
    orpheus_test_mixer_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        int32_t call=s->calls++;
        for(unsigned f=0;f<ctx->frame_count;f++) for(int32_t c=0;c<s->channels;c++) {
            unsigned i=f*s->channels+c;
            io->a.samples[i]=((int)((f*17+c*31)%103)-51)*.03125f+call*.125f;
            io->b.samples[i]=((int)((f*11+c*13)%97)-48)*.0625f-call*.25f;
        }
        int rc=call%2 ? orpheus_test_mixer_source_emit_b(ctx,&io->b)
            : orpheus_test_mixer_source_emit_a(ctx,&io->a);
        if(rc) return rc;
        for(unsigned i=0;i<ctx->frame_count*s->channels;i++)
            if(call%2) io->a.samples[i]-=.75f;
            else io->b.samples[i]+=.5f;
        rc=call%2 ? orpheus_test_mixer_source_emit_a(ctx,&io->a)
            : orpheus_test_mixer_source_emit_b(ctx,&io->b);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

UPDATE = r'''
    for(int tick=0;tick<2;tick++) {
        if(tick==1) {
            const char* ids[]={"gain0","gain1"};const float values[]={@G0@,@G1@};
            for(unsigned i=0;i<2;i++) {
                OrpheusValue value={ORPHEUS_VALUE_FLOAT};value.value.f32=values[i];
#ifdef DYNAMIC
                if(runtime.set_parameter("mixer",ids[i],value)) return 10;
#else
                if(orpheus_control_set_value("mixer",ids[i],value)) return 10;
#endif
            }
        }
'''


def oracle(channels, frames, gains, updated):
    current = [1.0, 1.0]
    expected = []
    for call in range(4):
        targets = [f32(10 ** f32(f32(db) / 20)) for db in (updated if call >= 2 else gains)]
        for delivery in range(2):
            row = []
            for frame in range(frames):
                for channel in range(channels):
                    a = f32(((frame * 17 + channel * 31) % 103 - 51) * .03125 + call * .125)
                    b = f32(((frame * 11 + channel * 13) % 97 - 48) * .0625 - call * .25)
                    if delivery:
                        if call % 2:
                            a = f32(a - .75)
                        else:
                            b = f32(b + .5)
                    current = [f32(old + f32(target - old)) for old, target in zip(current, targets)]
                    row.append(f32(f32(a * current[0]) + f32(b * current[1])))
            expected.append(row)
    return "{" + ",".join("{" + ",".join(v.hex() + "f" for v in row) + "}" for row in expected) + "}"


@pytest.mark.parametrize("channels,frames,gains,updated", [
    (1, 1, [0, 0], [20, -20]), (3, 17, [-96, 24], [-20, 0]),
    (32, 128, [24, -96], [0, 20]), (2, 7, [-6, 6], [6, -6]),
])
def test_mixer_each_input_arrival_four_entries(tmp_path, channels, frames, gains, updated):
    audio = {"type": "audio", "sample_format": "f32", "channels": "param:channels"}
    source = create_component(tmp_path, "mixer_source", {
        "a": {**audio, "direction": "output", "drive_port_no": 1},
        "b": {**audio, "direction": "output", "drive_port_no": 2},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
    }, SOURCE, source=True)
    capture_source = CAPTURE.replace("slew_capture", "mixer_capture").replace("[4]", "[8]")
    capture_source = capture_source.replace("s->calls>=4", "s->calls>=8").replace("s->calls/2", "s->calls/4")
    capture_source = capture_source.replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames))
    capture_source = capture_source.replace("@EXPECTED@", oracle(channels, frames, gains, updated))
    capture = create_component(tmp_path, "mixer_capture", {
        "in": {**audio, "direction": "input", "process": True},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, capture_source)
    manifest = ROOT / "components" / "orpheus" / "builtin" / "mixer" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.mixer_source"),
        "mixer": Node("mixer", "orpheus.builtin.mixer", params={
            "channels": channels, "gain0": float(gains[0]), "gain1": float(gains[1]),
        }),
        "capture": Node("capture", "orpheus.test.mixer_capture"),
    }, connections=[
        Connection(PortRef("source", "a"), PortRef("mixer", "in0")),
        Connection(PortRef("source", "b"), PortRef("mixer", "in1")),
        Connection(PortRef("mixer", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    update = UPDATE.replace("@G0@", f32(updated[0]).hex() + "f").replace("@G1@", f32(updated[1]).hex() + "f")
    harness = harness.replace("for(int tick=0;tick<2;tick++) {", update)
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 4, 0, 0], [1, 8, 0, frames]])
