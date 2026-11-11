"""同Task多次音频到达独立推进索引/显式两阶段滤波与包络。"""
import math
import struct
import wave

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_limiter_interfaces import CAPTURE
from test_table_interfaces import f32, four_entry_harness


@pytest.mark.parametrize("explicit,order,channels,frames,rate", [
    (False, 1, 3, 7, 8000), (True, 2, 64, 257, 48000),
])
def test_tone_state_advances_per_arrival(tmp_path, explicit, order, channels, frames, rate):
    boost, cut = list(map(f32, [.5, .25, .125, .1, .05])), list(map(f32, [.25, .1, .05, -.2, .1]))
    z1, z2, expected, pcm = [0.0] * channels, [0.0] * channels, [], []
    current = f32(math.pow(10, f32(6 / 20)))
    target, final = f32(1 + f32(4.7683716e-7)), f32(math.pow(10, f32(-9 / 20)))
    phase, ramps = 0, [[f32(.9), f32(.7), f32(1.001), 1], [f32(1.1), f32(1.4), f32(.999), 1]]

    def ramp_coefficient(start, end):
        blocks = max(1, math.ceil(f32(1 / f32(f32(1000 * frames) / rate))))
        coefficient = f32(math.exp(f32(f32(math.log(f32(end / start))) / f32(frames * blocks))))
        return blocks, coefficient

    remaining, coefficient = ramp_coefficient(current, target)
    for call in range(4):
        frozen = call >= 2
        if explicit:
            if not frozen and phase == 0 and ramps[0][3] == 0 and ramps[1][3] > 0:
                phase = 1
            gain, wanted, multiplier, count = ramps[phase]
            applied = 1.0
            if not frozen:
                if count == 0:
                    gain = wanted
                else:
                    applied = multiplier
                    ramps[phase][3] = max(-2147483648, count - 1)
            coefs = cut if gain < 1 else boost
        for n in range(frames):
            if explicit:
                gain = f32(gain * applied)
            else:
                if not frozen and remaining > 0:
                    current = f32(current * coefficient)
                gain = current
                coefs = boost if current >= 1 else cut
            for c in range(channels):
                x = f32((((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125)
                filtered = f32(f32(x * coefs[0]) + z1[c])
                if explicit and order == 1:
                    z1[c] = f32(f32(x * coefs[1]) + f32(coefs[3] * filtered))
                else:
                    first = z2[c] if explicit and coefs[1] == 0 else f32(f32(coefs[1] * x) + z2[c])
                    z1[c] = f32(first - f32(coefs[3] * filtered))
                    z2[c] = f32((f32(coefs[2] * x) if order > 1 else 0.0) - f32(coefs[4] * filtered))
                value = f32(x + f32(f32(gain - 1) * filtered))
                expected.append(value)
                pcm.append(int(f32(max(-1.0, min(1.0, value)) * 32767)))
        if explicit:
            ramps[phase][0] = gain
        elif not frozen and remaining > 0:
            remaining -= 1
            if remaining == 0:
                current = target
                remaining, coefficient = ramp_coefficient(current, final)
    source = create_component(tmp_path, "tone_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("delay_source", "tone_source").replace("@CHANNELS@", str(channels)).replace(
        "@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "tone_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("limiter_capture", "tone_capture").replace("@FRAMES@", str(frames)).replace(
        "@COUNT@", str(frames * channels)).replace("@EXPECTED@", ",".join(map(str, expected))))
    params = {"channels": channels, "execution_mode": int(explicit), "filter_order": order,
              "gain_index": 255.0, "max_boost_db": 6.0, "max_cut_db": 9.0, "ramp_ms": 1.0,
              "boost_coefs": ".5,.25,.125,.1,.05", "cut_coefs": ".25,.1,.05,-.2,.1"}
    if explicit:
        params.update(ramp0_current_gain=.9, ramp0_target_gain=.7, ramp0_coefficient=1.001,
                      ramp0_remaining_blocks=1, ramp1_current_gain=1.1, ramp1_target_gain=1.4,
                      ramp1_coefficient=.999, ramp1_remaining_blocks=1)
    manifests = [source, capture, *[ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                                   for name in ("indexed_tone", "wav_out")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", sample_rate=rate, block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.tone_source"),
        "tone": Node("tone", "orpheus.builtin.indexed_tone", params=params),
        "capture": Node("capture", "orpheus.test.tone_capture"),
        "sink": Node("sink", "orpheus.builtin.wav_out", params={
            "channels": channels, "sample_rate": 0, "file_path": str(tmp_path / "output.wav"),
        }),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("tone", "in")),
        Connection(PortRef("tone", "out"), PortRef("capture", "in")),
        Connection(PortRef("tone", "out"), PortRef("sink", "in")),
    ]))
    update = r'''
        OrpheusValue request={ORPHEUS_VALUE_BOOL};request.value.b=true;
        const char* key="freeze";
        if(tick==0 && !@EXPLICIT@) {
            request.type=ORPHEUS_VALUE_FLOAT;request.value.f32=0;key="gain_index";
        }
        if(tick==1 || !@EXPLICIT@) {
#ifdef DYNAMIC
            if(runtime.set_parameter("tone",key,request)) return 5;
#else
            if(orpheus_control_set_value("tone",key,request)) return 5;
#endif
        }
    '''
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})").replace(
        "for(int tick=0;tick<2;tick++) {",
        "for(int tick=0;tick<2;tick++) {" + update.replace("@EXPLICIT@", str(int(explicit))))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    baseline = None
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            result, rows = run_backend(case, backend, mode=mode)
            assert result.returncode == 0, (backend, mode, result.stdout, result.stderr)
            assert rows == [[0, 2, 0, 0], [1, 4, 0, frames]]
            data = (tmp_path / "output.wav").read_bytes()
            if baseline is None:
                baseline = data
            assert data == baseline
            with wave.open(str(tmp_path / "output.wav"), "rb") as stream:
                assert stream.getnchannels() == channels and stream.getnframes() == 4 * frames
                actual = struct.unpack(f"<{len(pcm)}h", stream.readframes(4 * frames))
            assert max(abs(a - b) for a, b in zip(actual, pcm)) <= 1
