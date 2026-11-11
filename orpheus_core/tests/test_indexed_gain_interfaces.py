"""查表增益的逐样本递推、逐到达块计数和冻结四入口验证。"""

import math

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, f32, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <math.h>
int orpheus_test_indexed_capture_algorithm(orpheus_test_indexed_capture_State* s,
    orpheus_test_indexed_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const float expected[]={@VALUES@};
    if(ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
        ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@)) return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<@COUNT@;n++) {
        float actual=io->in.samples[n],value=expected[s->calls*@COUNT@+n];
        if(!isfinite(actual) || fabsf(actual-value)>2e-6f) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->last=0;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,ramp_ms,rate,freeze", [
    (1, 8, 1, 0, False), (3, 64, 100, 50000, False),
    (64, 8, 2, 0, True), (3, 7, 0, 0, False),
])
def test_indexed_gain_each_delivery_advances_ramp_once(tmp_path, channels, frames, ramp_ms, rate, freeze):
    target, current = f32(10 ** (-15 / 20)), .5
    ratio = f32(target / current)
    difference = f32(abs(f32(20 * f32(math.log10(ratio)))))
    ms = f32(f32(difference / min(50000, max(5, rate))) * 1000) if rate else ramp_ms
    blocks = 0 if ramp_ms <= 0 else max(1, math.ceil(f32(max(1, min(3000, ms)) /
                                                                         f32(f32(1000 * frames) / 48000))))
    coefficient = f32(math.exp(f32(f32(math.log(ratio)) / f32(frames * blocks)))) if blocks else 1
    if not blocks:
        current = target
    expected = []
    for call in range(4):
        frozen = freeze and call >= 2
        for n in range(frames):
            if blocks and not frozen:
                current = f32(current * coefficient)
            for c in range(channels):
                sample = (((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125
                expected.append(f32(sample * current))
        if blocks and not frozen:
            blocks -= 1
            if not blocks:
                current = target
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "indexed_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@VALUES@", ",".join(format(value, ".9g") for value in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    gain = ROOT / "components" / "orpheus" / "builtin" / "indexed_gain" / "component.yaml"
    manifests = [source, gain, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "gain": Node("gain", "orpheus.builtin.indexed_gain", params={
            "channels": channels, "table_idx": ",".join(map(str, [0, 128] + [255] * 30)),
            "table_db": ",".join(map(str, [-24, -12] + [0] * 30)),
            "gain_offset_db": -3.0, "apply_index_on_prepare": False, "initial_gain": .5,
            "ramp_ms": float(ramp_ms), "ramp_db_per_s": float(rate),
        }),
        "capture": Node("capture", "orpheus.test.indexed_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("gain", "in")),
        Connection(PortRef("gain", "out"), PortRef("capture", "in")),
    ]))
    updates = r'''
        OrpheusValue update={ORPHEUS_VALUE_FLOAT};update.value.f32=128;
        if(!tick) {
#ifdef DYNAMIC
            if(runtime.set_parameter("gain","gain_index",update)) return 8;
#else
            if(orpheus_control_set_value("gain","gain_index",update)) return 8;
#endif
        } else if(@FREEZE@) {
            update.type=ORPHEUS_VALUE_BOOL;update.value.b=true;
#ifdef DYNAMIC
            if(runtime.set_parameter("gain","freeze",update)) return 9;
#else
            if(orpheus_control_set_value("gain","freeze",update)) return 9;
#endif
        }
'''
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("    for(int tick=0;tick<2;tick++) {",
                              "    for(int tick=0;tick<2;tick++) {" + updates.replace("@FREEZE@", str(int(freeze))))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
