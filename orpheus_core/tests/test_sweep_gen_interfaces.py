"""线性/对数扫频根Task及扫频结束静音的四入口验证。"""

import math

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_signal_gen_interfaces import CAPTURE
from test_table_interfaces import check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("channels,frames,rate,start,end,log_scale", [
    (1, 7, 48000, 50, 900, False), (32, 257, 48000, 18000, 100, True),
    (3, 512, 8000, 100, 3000, True),
])
def test_sweep_root_preserves_double_time_and_end_silence(tmp_path, channels, frames, rate, start, end, log_scale):
    duration = f32(.1)
    t, phase, expected, frequencies, progress = 0.0, 0.0, [], [], []
    for tick in range(2):
        for _ in range(frames):
            frequency = (0 if t >= duration else start * (end / start) ** (t / duration) if log_scale
                         else start + (end - start) * (t / duration))
            expected.append(f32((f32(.7) if tick == 0 else .125) * math.sin(phase)) if frequency > 0 else 0)
            phase += 2 * math.pi * frequency * (1 / rate)
            if phase > 2 * math.pi * 64:
                phase -= 2 * math.pi * 64
            t += 1 / rate
        frequencies.append(f32(frequency))
        progress.append(min(1, f32(t / duration)))
    code = CAPTURE.replace("orpheus_test_sine_capture", "orpheus_test_sweep_capture")
    code = code.replace("@VALUES@", ",".join(format(value, ".9g") for value in expected))
    for key, value in (("FRAMES", frames), ("RATE", rate), ("CHANNELS", channels)):
        code = code.replace(f"@{key}@", str(value))
    capture = create_component(tmp_path, "sweep_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code)
    source = ROOT / "components" / "orpheus" / "builtin" / "sweep_gen" / "component.yaml"
    registry = Registry()
    for manifest in (source, capture):
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, sample_rate=rate, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.sweep_gen", params={
            "channels": channels, "sample_rate": rate, "start_freq": float(start), "end_freq": float(end),
            "duration_s": .1, "log_scale": log_scale,
        }),
        "capture": Node("capture", "orpheus.test.sweep_capture"),
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
    probe = r'''
        const float expected_progress[]={@PROGRESS@},expected_frequency[]={@FREQUENCIES@};
        const char* keys[]={"progress","current_freq"};
        for(int p=0;p<2;p++) {
            OrpheusValue v={ORPHEUS_VALUE_FLOAT};
#ifdef DYNAMIC
            if(runtime.get_parameter("source",keys[p],&v)) return 9;
#else
            if(orpheus_control_get_value("source",keys[p],&v)) return 9;
#endif
            float expected=p ? expected_frequency[tick] : expected_progress[tick];
            if(v.type!=ORPHEUS_VALUE_FLOAT || !isfinite(v.value.f32) ||
                fabsf(v.value.f32-expected)>1e-5f*fmaxf(1,expected)) return 10;
        }
'''
    harness = "#include <math.h>\n" + four_entry_harness()
    harness = harness.replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace(f"48000,{frames}", f"{rate},{frames}")
    harness = harness.replace("    for(int tick=0;tick<2;tick++) {",
                              "    for(int tick=0;tick<2;tick++) {" + update)
    harness = harness.replace("        printf(", probe.replace("@PROGRESS@", ",".join(map(str, progress)))
                              .replace("@FREQUENCIES@", ",".join(map(str, frequencies))) + "        printf(")
    case = compile_delivery_backends(tmp_path, registry, project, [source, capture], harness)
    check_four_entries(case, [[0, 1, 0, 0], [1, 2, 0, frames]])
