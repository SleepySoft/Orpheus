"""同Task两次音频到达，独立逐样本参考与块末增益交付顺序。"""
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, f32, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <math.h>
int orpheus_test_gain_capture_algorithm(orpheus_test_gain_capture_State* s,
    orpheus_test_gain_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const float samples[]={@SAMPLES@},gains[]={@GAINS@};
    unsigned call=(unsigned)s->calls/2,port=(unsigned)s->calls%2;
    if(call>=4 || ctx->trigger_input!=port || ctx->frame_count!=7 || ctx->valid_frames!=7 ||
        ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(call/2)*7) return ORPHEUS_ERR_PROCESSING;
    if(!port) {
        if(io->in.buffer->frame_count!=7) return ORPHEUS_ERR_PROCESSING;
        for(unsigned n=0;n<21;n++)
            if(!isfinite(io->in.samples[n]) || fabsf(io->in.samples[n]-samples[call*21+n])>1e-6f)
                return ORPHEUS_ERR_PROCESSING;
    } else if(io->gain.buffer->frame_count!=1 || !isfinite(io->gain.samples[0]) ||
        io->gain.samples[0]!=gains[call]) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def test_each_audio_arrival_delivers_audio_then_applied_gain(tmp_path):
    gains, targets, coefficients, remaining = list(map(f32, [.8, .5])), list(map(f32, [.2, .3])), list(map(f32, [1.01, .99])), [1, 2]
    samples, observed = [], []
    for call in range(4):
        steps = [1.0, 1.0]
        for r in range(2):
            if not remaining[r]:
                gains[r] = targets[r]
            else:
                remaining[r] -= 1
                steps[r] = coefficients[r]
        for n in range(7):
            gains = [f32(gain * step) for gain, step in zip(gains, steps)]
            for c in range(3):
                value = (((call * 7 + n) * 17 + c * 31) % 103 - 51) * .03125
                samples.append(value if c == 1 else f32(value * gains[c // 2]))
        observed.append(gains[1])
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=3, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", "3").replace("@FRAMES@", "7"), source=True)
    capture = create_component(tmp_path, "gain_capture", {
        "in": audio("input", channels=3, process=True),
        "gain": {"direction": "input", "type": "control", "sample_format": "f32",
                 "channels": 1, "block_size": 1, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@SAMPLES@", ",".join(map(str, samples))).replace("@GAINS@", ",".join(map(str, observed))))
    gain = ROOT / "components" / "orpheus" / "builtin" / "gain_ramper" / "component.yaml"
    manifests = [source, gain, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=7, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "gain": Node("gain", "orpheus.builtin.gain_ramper", params={
            "channels": 3, "num_rampers": 2, "chan_map": "0,-1,1", "execution_mode": 1,
            "current_gains": ".8,.5", "target_gains": ".2,.3", "coefficients": "1.01,.99",
            "remaining_blocks": "1,2", "gain_output": 1, "gain_channel": 2,
        }),
        "capture": Node("capture", "orpheus.test.gain_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("gain", "in")),
        Connection(PortRef("gain", "out"), PortRef("capture", "in")),
        Connection(PortRef("gain", "gain0"), PortRef("capture", "gain")),
    ]))
    harness = four_entry_harness().replace(",8)", ",7)").replace("(8)", "(7)")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 4, 0, 0], [1, 8, 0, 7]])
