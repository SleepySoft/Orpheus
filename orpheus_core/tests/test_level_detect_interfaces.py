"""平滑包络及三个真实出口的同Task多次到达四入口验证。"""

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
int orpheus_test_level_capture_algorithm(orpheus_test_level_capture_State* s,
    orpheus_test_level_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const float rms[]={@RMS@},peak[]={@PEAK@};
    unsigned call=s->calls/@OUTLETS@,port=s->calls%@OUTLETS@;
    if(ctx->trigger_input!=port || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ ||
        ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(call/2*@FRAMES@)) return ORPHEUS_ERR_PROCESSING;
    if(!port) {
        for(unsigned n=0;n<@FRAMES@;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
            float value=(float)((int)(((call*@FRAMES@+n)*17+c*31)%103)-51)*.03125f;
            if(io->in.samples[n*@CHANNELS@+c]!=value) return ORPHEUS_ERR_PROCESSING;
        }
    } else {
        const OrpheusBuffer* buffer=port==1 ? io->rms.buffer : io->peak.buffer;
        float actual=port==1 ? io->rms.samples[0] : io->peak.samples[0];
        if(buffer->frame_count!=1 || !isfinite(actual) ||
            fabsf(actual-(port==1 ? rms[call] : peak[call]))>2e-5f) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,mode,execution", [(1, 1, 0, 0), (32, 7, 1, 0), (32, 8, 0, 1)])
def test_each_arrival_updates_level_and_explicit_statistics(tmp_path, channels, frames, mode, execution):
    def coefficient(ms):
        scale = f32(f32(ms / 1000) * 48000)
        return f32(1 - f32(math.exp(f32(-1 / scale))))
    attack, release = coefficient(10), coefficient(100)
    env, levels, rms_values, peak_values = [0.0] * channels, [], [], []
    epsilon = f32(2.22044605e-16)
    for call in range(4):
        samples = [[(((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125
                    for c in range(channels)] for n in range(frames)]
        powers = []
        for c in range(channels):
            power = 0.0
            for row in samples:
                power = f32(power + f32(row[c] * row[c]))
            powers.append(f32(power / frames))
        rms = f32(f32(math.log10(f32(max(powers) + epsilon))) * 10)
        peak = f32(f32(math.log10(f32(max(abs(value) for row in samples for value in row) + epsilon))) * 20)
        rms_values.append(rms)
        peak_values.append(peak)
        maximum = 0.0
        if execution:
            maximum = rms
        elif mode:
            for c, power in enumerate(powers):
                env[c] = f32(env[c] + f32(attack * f32(f32(math.sqrt(power)) - env[c])))
                maximum = max(maximum, env[c])
        else:
            for row in samples:
                for c, value in enumerate(row):
                    magnitude = abs(value)
                    rate = attack if magnitude > env[c] else release
                    env[c] = f32(env[c] + f32(rate * f32(magnitude - env[c])))
                    maximum = max(maximum, env[c])
        levels.append(maximum)
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    control = {"direction": "input", "type": "control", "sample_format": "f32",
               "channels": 1, "block_size": 1, "process": True}
    code = CAPTURE
    for key, value in (("RMS", ",".join(map(str, rms_values))), ("PEAK", ",".join(map(str, peak_values))),
                       ("OUTLETS", 3 if execution else 1), ("FRAMES", frames), ("CHANNELS", channels)):
        code = code.replace(f"@{key}@", str(value))
    capture = create_component(tmp_path, "level_capture", {
        "in": audio("input", channels=channels, process=True), "rms": control, "peak": control,
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code)
    detector = ROOT / "components" / "orpheus" / "builtin" / "level_detect" / "component.yaml"
    manifests = [source, detector, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    edges = [Connection(PortRef("source", "out"), PortRef("level", "in")),
             Connection(PortRef("level", "out"), PortRef("capture", "in"))]
    if execution:
        edges += [Connection(PortRef("level", name + "0"), PortRef("capture", name)) for name in ("rms", "peak")]
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "level": Node("level", "orpheus.builtin.level_detect", params={
            "channels": channels, "mode": mode, "execution_mode": execution,
        }),
        "capture": Node("capture", "orpheus.test.level_capture"),
    }, connections=edges))
    probe = r'''
        OrpheusValue level={ORPHEUS_VALUE_FLOAT};const float expected[]={@LEVELS@};
#ifdef DYNAMIC
        if(runtime.get_parameter("level","level",&level)) return 8;
#else
        if(orpheus_control_get_value("level","level",&level)) return 8;
#endif
        if(!isfinite(level.value.f32) || fabsf(level.value.f32-expected[tick])>2e-5f) return 9;
'''
    harness = "#include <math.h>\n" + four_entry_harness()
    harness = harness.replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("        printf(",
                              probe.replace("@LEVELS@", f"{levels[1]},{levels[3]}") + "        printf(")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    outlets = 3 if execution else 1
    check_four_entries(case, [[0, 2 * outlets, 0, 0], [1, 4 * outlets, 0, frames]])
