"""多相FIR对照独立卷积预期，并验证重复交付与本地有理速率时间。"""
import math

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("channels,frames,up,down,taps", [
    (3, 13, 3, 13, 16), (64, 3, 13, 3, 32), (64, 7, 1, 1, 16),
])
def test_polyphase_each_arrival_matches_independent_convolution_and_local_time(tmp_path, channels, frames, up, down, taps):
    output_frames = frames * up // down
    samples = [f32((((n * 17 + c * 31) % 103) - 51) * .03125)
               for n in range(4 * frames) for c in range(channels)]
    for block in range(4):
        samples[block * frames * channels] = -0.0
    coefficients = []
    cutoff = .45 * min(1.0, up / down)
    for phase in range(up):
        values = []
        for tap in range(taps):
            angle = 2 * math.pi * cutoff * (tap - .5 * (taps - 1) + phase / up)
            sinc = 1 if abs(angle) < 1e-12 else math.sin(angle) / angle
            w = 2 * math.pi * tap / (taps - 1)
            window = .42 - .5 * math.cos(w) + .08 * math.cos(2 * w)
            values.append(2 * cutoff * sinc * window)
        total = f32(sum(values))
        coefficients.append([f32(f32(value) / total) for value in values])
    expected = []
    for n in range(4 * output_frames):
        sample, phase = divmod(n * down, up)
        for c in range(channels):
            if up == down:
                value = samples[sample * channels + c]
            else:
                value = 0.0
                for tap in range(taps):
                    if sample >= tap:
                        value += samples[(sample - tap) * channels + c] * coefficients[phase][tap]
                value = f32(value)
            expected.append(value)
    source = create_component(tmp_path, "rational_source", {
        "out": audio("output", channels=channels, block_size=frames, drive_port_no=1),
        "calls": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_rational_source_algorithm(orpheus_test_rational_source_State* s,
    orpheus_test_rational_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;++pass) {
        for(unsigned f=0;f<@FRAMES@;++f) for(unsigned c=0;c<@CHANNELS@;++c)
            io->out.samples[f*@CHANNELS@+c]=
                (float)((int)(((s->calls*@FRAMES@+f)*17+c*31)%103)-51)*.03125f;
        io->out.samples[0]=-0.0f;
        io->out.delivery.frame_index=(uint64_t)s->calls*@FRAMES@;
        if(s->calls) io->out.delivery.timeline_flags&=~ORPHEUS_TIMELINE_DISCONTINUITY;
        int rc=orpheus_test_rational_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        ++s->calls;
    }
    return ORPHEUS_OK;
}
'''.replace("@FRAMES@", str(frames)).replace("@CHANNELS@", str(channels)), source=True)
    capture = create_component(tmp_path, "rational_capture", {
        "in": audio("input", channels=channels, block_size=output_frames, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0), "frame": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include <math.h>
#include <string.h>
int orpheus_test_rational_capture_algorithm(orpheus_test_rational_capture_State* s,
    orpheus_test_rational_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(s->calls>=4 || ctx->trigger_input!=0 || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ ||
       ctx->epoch!=1 || ctx->sample_rate!=48000.0*@UP@/@DOWN@ ||
       ctx->frame_index!=(uint64_t)s->calls*@OUTPUT@ ||
       ctx->timeline_flags!=(s->calls ? 0u : ORPHEUS_TIMELINE_DISCONTINUITY) ||
       io->in.buffer->frame_count!=@OUTPUT@) return ORPHEUS_ERR_PROCESSING;
    const uint32_t* row=expected+s->calls*@COUNT@;
    if(@IDENTITY@) {
        if(memcmp(io->in.samples,row,@COUNT@*4)) return ORPHEUS_ERR_PROCESSING;
    } else for(unsigned n=0;n<@COUNT@;++n) {
        float target;memcpy(&target,row+n,4);
        if(!isfinite(io->in.samples[n]) || fabsf(io->in.samples[n]-target)>2e-6f)
            return ORPHEUS_ERR_PROCESSING;
    }
    ++s->calls;s->last=io->in.samples[@COUNT@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''.replace("@EXPECTED@", ",".join(str(bits(value)) for value in expected))
        .replace("@FRAMES@", str(frames)).replace("@UP@", str(up)).replace("@DOWN@", str(down))
        .replace("@OUTPUT@", str(output_frames)).replace("@COUNT@", str(output_frames * channels))
        .replace("@IDENTITY@", str(int(up == down))))
    resample = ROOT / "components" / "orpheus" / "builtin" / "rational_resample" / "component.yaml"
    manifests = [source, resample, capture]
    registry = Registry()
    for path in manifests:
        registry.add_manifest(path)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.rational_source"),
        "resample": Node("resample", "orpheus.builtin.rational_resample", params={
            "channels": channels, "input_frames": frames, "up": up, "down": down, "taps": taps}),
        "capture": Node("capture", "orpheus.test.rational_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("resample", "in")),
        Connection(PortRef("resample", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert case[2].node_configs["resample"]["frames"] == frames
    assert case[2].node_configs["resample"]["period"] == 1
    check_four_entries(case, [
        [0, 2, pytest.approx(expected[2 * output_frames * channels - 1], abs=2e-6), output_frames],
        [1, 4, pytest.approx(expected[-1], abs=2e-6), 3 * output_frames],
    ])
