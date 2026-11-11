"""真实正弦根 Task 的逐样本 f32 相位与跨调用振幅更新验证。"""

import math

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, f32, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <math.h>
int orpheus_test_sine_capture_algorithm(orpheus_test_sine_capture_State* s,
    orpheus_test_sine_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const float expected[]={@VALUES@};
    if(ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ || ctx->sample_rate!=@RATE@ ||
        ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls*@FRAMES@))
        return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<@FRAMES@;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
        float actual=io->in.samples[n*@CHANNELS@+c];
        if(!isfinite(actual) || fabsf(actual-expected[s->calls*@FRAMES@+n])>2e-6f)
            return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,rate,frequency", [
    (1, 1, 48000, 440), (32, 257, 48000, 937), (3, 17, 8000, 18000),
])
def test_real_synthetic_source_root_preserves_phase_and_amplitude_update(tmp_path, channels, frames, rate, frequency):
    phase, expected = 0.0, []
    two_pi = f32(2 * f32(math.pi))
    step = f32(f32(two_pi * frequency) / rate)
    for tick in range(2):
        for _ in range(frames):
            expected.append(f32((.5 if tick == 0 else .125) * f32(math.sin(phase))))
            phase = f32(phase + step)
            if phase > two_pi:
                phase = f32(phase - two_pi)
    code = CAPTURE.replace("@VALUES@", ",".join(format(value, ".9g") for value in expected))
    for key, value in (("FRAMES", frames), ("RATE", rate), ("CHANNELS", channels)):
        code = code.replace(f"@{key}@", str(value))
    capture = create_component(tmp_path, "sine_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code)
    source = ROOT / "components" / "orpheus" / "builtin" / "signal_gen" / "component.yaml"
    registry = Registry()
    for manifest in (source, capture):
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, sample_rate=rate, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.signal_gen", params={
            "channels": channels, "sample_rate": rate, "frequency": float(frequency),
            "duration_s": .25,
        }),
        "capture": Node("capture", "orpheus.test.sine_capture"),
    }, connections=[Connection(PortRef("source", "out"), PortRef("capture", "in"))]))
    update = r'''
        if(tick) {
            OrpheusValue amplitude={ORPHEUS_VALUE_FLOAT};amplitude.value.f32=.125f;
#ifdef DYNAMIC
            if(runtime.set_parameter("source","amplitude",amplitude)) return 8;
#else
            if(orpheus_control_set_value("source","amplitude",amplitude)) return 8;
#endif
        }
'''
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace(f"48000,{frames}", f"{rate},{frames}")
    harness = harness.replace("    for(int tick=0;tick<2;tick++) {",
                              "    for(int tick=0;tick<2;tick++) {" + update)
    case = compile_delivery_backends(tmp_path, registry, project, [source, capture], harness)
    assert case[2].duration_frames == int(rate * .25)
    check_four_entries(case, [[0, 1, 0, 0], [1, 2, 0, frames]])
