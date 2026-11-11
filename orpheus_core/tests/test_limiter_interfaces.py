"""共享交错包络与逐通道重复系数：真实重复到达和独立f32参考。"""
import math
import struct
import wave

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import f32, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <math.h>
int orpheus_test_limiter_capture_algorithm(orpheus_test_limiter_capture_State* s,
    orpheus_test_limiter_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const float expected[]={@EXPECTED@};
    if(s->calls>=4 || ctx->trigger_input!=0 || ctx->frame_count!=@FRAMES@ ||
       ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
       ctx->frame_index!=(uint64_t)(s->calls/2)*@FRAMES@) return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<@COUNT@;n++) {
        float actual=io->in.samples[n],value=expected[s->calls*@COUNT@+n];
        if(!isfinite(actual) || fabsf(actual-value)>2e-6f ||
            (value==0 && signbit(actual)!=signbit(value))) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("mode,channels,frames,rate", [
    ("shared", 3, 7, 8000), ("per_channel", 32, 257, 48000),
])
def test_peak_envelopes_advance_per_arrival(tmp_path, mode, channels, frames, rate):
    threshold = f32(math.pow(10, f32(-35 / 20)))
    envelope = [0.0] * channels
    expected, pcm = [], []
    for call, amplitude in enumerate(map(f32, [1, .01, 0, 1.5])):
        for n in range(frames):
            for c in range(channels):
                x = f32((((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125 * amplitude)
                index = 0 if mode == "shared" else c
                magnitude = abs(x)
                if mode == "shared":
                    ms = f32(.5 if magnitude > envelope[index] else 20.0)
                    denominator = f32(f32(ms / 1000) * rate)
                    coefficient = f32(1 - f32(math.exp(f32(-1 / denominator))))
                else:
                    coefficient = f32(([.1, .2, .3] if magnitude > envelope[index] else [.01, .02])[min(c, 2 if magnitude > envelope[index] else 1)])
                envelope[index] = f32(envelope[index] + f32(coefficient * f32(magnitude - envelope[index])))
                gain = 1.0
                if envelope[index] > threshold and envelope[index] > 0:
                    gain = f32(threshold / envelope[index]) if mode == "shared" else max(
                        f32(f32(f32(.8 if c == 0 else 1.1) * threshold) / envelope[index]),
                        f32(.2 if c == 0 else .3))
                value = f32(x * gain)
                expected.append(value)
                pcm.append(int(f32(max(-1.0, min(1.0, value)) * 32767)))
    source_code = SOURCE.replace("for(unsigned pass=0;", "static const float amplitudes[]={1,.01f,0,1.5f};\n    for(unsigned pass=0;")
    source_code = source_code.replace("*.03125f;", "*.03125f*amplitudes[s->calls];")
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, source_code.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "limiter_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@FRAMES@", str(frames)).replace("@COUNT@", str(frames * channels)).replace(
        "@EXPECTED@", ",".join(map(str, expected))))
    manifests = [source, capture, *[ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                                   for name in ("limiter", "wav_out")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", sample_rate=rate, block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "limiter": Node("limiter", "orpheus.builtin.limiter", params={
            "channels": channels, "mode": mode, "threshold_db": -35.0, "attack_ms": .5, "release_ms": 20.0,
            "attack_coeffs": ".1 .2,.3", "release_coeffs": ".01,.02", "k1": ".8,1.1", "max_attack": ".2,.3",
        }),
        "capture": Node("capture", "orpheus.test.limiter_capture"),
        "sink": Node("sink", "orpheus.builtin.wav_out", params={
            "channels": channels, "sample_rate": 0, "file_path": str(tmp_path / "output.wav"),
        }),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("limiter", "in")),
        Connection(PortRef("limiter", "out"), PortRef("capture", "in")),
        Connection(PortRef("limiter", "out"), PortRef("sink", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
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
