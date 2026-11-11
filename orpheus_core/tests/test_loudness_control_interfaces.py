"""每次音频到达独立推进RMS控制，逐项f32参考及控制探针四入口验证。"""
import math

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, f32, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
int orpheus_test_lgc_capture_algorithm(orpheus_test_lgc_capture_State* s,
    orpheus_test_lgc_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(ctx->trigger_input!=0 || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ ||
       ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/2)*@FRAMES@) return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<@FRAMES@;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
        float expected=(float)((int)(((s->calls*@FRAMES@+n)*17+c*31)%103)-51)*.03125f;
        if(io->in.samples[n*@CHANNELS@+c]!=expected) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,rate", [(1, 1, 48000), (32, 257, 8000)])
def test_each_arrival_updates_control_without_applying_audio_gain(tmp_path, channels, frames, rate):
    levels = list(map(f32, [.05, .5, 0, .01]))
    energy, gain = 0.0, 0.0
    expected = []

    def db(value):
        return -120.0 if value <= f32(1e-12) else f32(10 * f32(math.log10(value)))

    def coefficient(ms):
        denominator = f32(f32(ms * f32(.001)) * rate)
        return f32(1 - f32(math.exp(f32(-frames / denominator))))

    for level in levels:
        for _ in range(2):
            raw = f32(level * level)
            if energy <= f32(1e-12) < raw:
                energy = raw
            else:
                c = coefficient(5.0 if raw > energy else 20.0)
                energy = f32(energy + f32(c * f32(raw - energy)))
            input_db = db(energy)
            desired = gain
            if db(raw) >= -55.0:
                desired = min(12.0, max(-12.0, f32(-18.0 - input_db)))
            c = coefficient(5.0 if desired < gain else 50.0)
            gain = f32(gain + f32(c * f32(desired - gain)))
            output_db = f32(input_db + gain) if input_db > -120.0 else -120.0
        expected.append([input_db, gain, output_db])
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "lgc_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)))
    control = ROOT / "components" / "orpheus" / "builtin" / "loudness_gain_control" / "component.yaml"
    manifests = [source, control, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", sample_rate=rate, block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "control": Node("control", "orpheus.builtin.loudness_gain_control", params={
            "channels": channels, "target_db": -18.0, "detector_attack_ms": 5.0,
            "detector_release_ms": 20.0, "gain_attack_ms": 5.0, "gain_release_ms": 50.0,
        }),
        "capture": Node("capture", "orpheus.test.lgc_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("control", "in")),
        Connection(PortRef("control", "out"), PortRef("capture", "in")),
    ]))
    before = r'''
    for(int tick=0;tick<4;tick++) {
        static const float levels[]={@LEVELS@};
        OrpheusValue level={ORPHEUS_VALUE_FLOAT};level.value.f32=levels[tick];
#ifdef DYNAMIC
        if(runtime.set_parameter("control","level",level)) return 8;
#else
        if(orpheus_control_set_value("control","level",level)) return 8;
#endif
'''
    after = r'''
        static const float expected[][3]={@EXPECTED@};
        const char* keys[]={"input_db","gain_db","output_db"};
        for(unsigned n=0;n<3;n++) {
            OrpheusValue value={ORPHEUS_VALUE_FLOAT};
#ifdef DYNAMIC
            if(runtime.get_parameter("control",keys[n],&value)) return 9;
#else
            if(orpheus_control_get_value("control",keys[n],&value)) return 9;
#endif
            if(!isfinite(value.value.f32) || fabsf(value.value.f32-expected[tick][n])>2e-5f) return 10;
        }
'''
    before = before.replace("@LEVELS@", ",".join(map(str, levels)))
    after = after.replace("@EXPECTED@", ",".join("{" + ",".join(map(str, row)) + "}" for row in expected))
    harness = "#include <math.h>\n" + four_entry_harness()
    assert "for(int tick=0;tick<2;tick++) {" in harness
    harness = harness.replace("for(int tick=0;tick<2;tick++) {", before).replace("        printf(", after + "        printf(")
    harness = harness.replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[tick, (tick + 1) * 2, 0, tick * frames] for tick in range(4)])
