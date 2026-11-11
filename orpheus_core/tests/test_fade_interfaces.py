"""主入口独立驱动：内部滤波历史、外部只读低频及同Task两次包络推进。"""
import math
import struct
import wave

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_limiter_interfaces import CAPTURE
from test_table_interfaces import f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_fade_source_algorithm(orpheus_test_fade_source_State* s,
    orpheus_test_fade_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
            unsigned i=n*@CHANNELS@+c;
            float x=(float)((int)(((s->calls*@FRAMES@+n)*17+c*31)%103)-51)*.03125f;
            io->out.samples[i]=x;
            @LOW_WRITE@
        }
        @LOW_EMIT@
        int rc=orpheus_test_fade_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("external,channels,frames,rate", [(False, 32, 257, 8000), (True, 3, 7, 48000)])
def test_fade_main_input_is_the_only_driver(tmp_path, external, channels, frames, rate):
    a1 = f32(math.exp(f32(f32(f32(-2 * f32(math.pi)) * 400) / rate)))
    smoothing = f32(1 - f32(math.exp(f32(-1 / f32(f32(50 / 1000) * rate)))))
    z, front, back = [0.0] * channels, 1.0, f32(1 - f32(.8))
    current, targets = list(map(f32, [.8, .7, .6, .5])), list(map(f32, [.3, .2, .1, .4]))
    coefficients, remaining = list(map(f32, [1.01, .99, 1.005, .98])), [3, 1]
    expected, pcm = [], []
    for call in range(4):
        if external:
            starts, multipliers = current[:], [1.0] * 4
            for r in range(2):
                if call < 2:
                    if remaining[r] == 0:
                        starts[r], starts[r + 2] = targets[r], targets[r + 2]
                    else:
                        remaining[r] -= 1
                        multipliers[r], multipliers[r + 2] = coefficients[r], coefficients[r + 2]
        for n in range(frames):
            if external:
                gains = [f32(value * (1 if coefficient == 1 else f32(math.pow(coefficient, n + 1))))
                         for value, coefficient in zip(starts, multipliers)]
            else:
                front = f32(front + f32(smoothing * f32((1 if call < 2 else f32(1 + f32(-.6))) - front)))
                back = f32(back + f32(smoothing * f32((f32(1 - f32(.8)) if call < 2 else 1) - back)))
            for c in range(channels):
                x = f32((((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125)
                if external:
                    low = f32(f32(x * .25) + (c % 3 - 1) * .03125)
                    value = 0.0 if c == 2 else f32(f32(low * gains[c + 2]) + f32(f32(x - low) * gains[c]))
                else:
                    low = f32(f32(f32(1 - a1) * x) + f32(a1 * z[c]))
                    z[c] = low
                    value = f32(low + f32((front if c < channels - 1 else back) * f32(x - low)))
                expected.append(value)
                pcm.append(int(f32(max(-1.0, min(1.0, value)) * 32767)))
        if external:
            current = gains
    source_interfaces = {"out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0)}
    if external:
        source_interfaces["low"] = audio("output", channels=channels, drive_port_no=2)
    source_code = SOURCE.replace("@LOW_WRITE@", "io->low.samples[i]=x*.25f+(float)((int)(c%3)-1)*.03125f;" if external else "")
    source_code = source_code.replace("@LOW_EMIT@", "int low_rc=orpheus_test_fade_source_emit_low(ctx,&io->low);if(low_rc)return low_rc;" if external else "")
    source = create_component(tmp_path, "fade_source", source_interfaces,
                              source_code.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fade_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("limiter_capture", "fade_capture").replace("@FRAMES@", str(frames)).replace(
        "@COUNT@", str(frames * channels)).replace("@EXPECTED@", ",".join(map(str, expected))))
    manifests = [source, capture, *[ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                                   for name in ("fade", "wav_out")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    connections = [Connection(PortRef("source", "low"), PortRef("fade", "lpf0"))] if external else []
    connections += [
        Connection(PortRef("source", "out"), PortRef("fade", "in")),
        Connection(PortRef("fade", "out"), PortRef("capture", "in")),
        Connection(PortRef("fade", "out"), PortRef("sink", "in")),
    ]
    params = {"channels": channels, "execution_mode": int(external), "fade": .8,
              "front_channels": channels - 1, "crossover_hz": 400.0, "ramp_ms": 50.0}
    if external:
        params.update(chan_map="0,1,-1", current_gains=".8,.7,.6,.5", target_gains=".3,.2,.1,.4",
                      coefficients="1.01,.99,1.005,.98", remaining_blocks="3,1,77,-123")
    project = Project(flow_mode="task_sequence", sample_rate=rate, block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.fade_source"),
        "fade": Node("fade", "orpheus.builtin.fade", params=params),
        "capture": Node("capture", "orpheus.test.fade_capture"),
        "sink": Node("sink", "orpheus.builtin.wav_out", params={
            "channels": channels, "sample_rate": 0, "file_path": str(tmp_path / "output.wav"),
        }),
    }, connections=connections))
    update = ('''
        if(tick==1) {
            OrpheusValue request={ORPHEUS_VALUE_BOOL};request.value.b=true;
    ''' if external else '''
        if(tick==1) {
            OrpheusValue request={ORPHEUS_VALUE_FLOAT};request.value.f32=-.6f;
    ''') + '''
#ifdef DYNAMIC
            if(runtime.set_parameter("fade","@KEY@",request)) return 5;
#else
            if(orpheus_control_set_value("fade","@KEY@",request)) return 5;
#endif
        }
    '''
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})").replace(
        "for(int tick=0;tick<2;tick++) {", "for(int tick=0;tick<2;tick++) {" + update.replace("@KEY@", "freeze" if external else "fade"))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    baseline = None
    for backend in ("dynamic", "generated_backend"):
        for entry in (0, 1):
            result, rows = run_backend(case, backend, mode=entry)
            assert result.returncode == 0, (backend, entry, result.stdout, result.stderr)
            assert rows == [[0, 2, 0, 0], [1, 4, 0, frames]]
            data = (tmp_path / "output.wav").read_bytes()
            if baseline is None:
                baseline = data
            assert data == baseline
            with wave.open(str(tmp_path / "output.wav"), "rb") as stream:
                assert stream.getframerate() == rate and stream.getnchannels() == channels
                actual = struct.unpack(f"<{len(pcm)}h", stream.readframes(4 * frames))
            assert max(abs(a - b) for a, b in zip(actual, pcm)) <= 1
