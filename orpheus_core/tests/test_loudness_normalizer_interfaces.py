"""联合RMS与块内增益插值：真实重复到达、独立f32参考及WAV逐字节一致。"""
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
int orpheus_test_ln_capture_algorithm(orpheus_test_ln_capture_State* s,
    orpheus_test_ln_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const float gains[]={@GAINS@},amplitudes[]={.01f,.8f,0,.03f};
    if(s->calls>=4 || ctx->trigger_input!=0 || ctx->frame_count!=@FRAMES@ ||
       ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
       ctx->frame_index!=(uint64_t)(s->calls/2)*@FRAMES@) return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<@FRAMES@;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
        float sample=(float)((int)(((s->calls*@FRAMES@+n)*17+c*31)%103)-51)*.03125f*amplitudes[s->calls];
        float expected=sample*gains[s->calls*@FRAMES@+n],actual=io->in.samples[n*@CHANNELS@+c];
        if(!isfinite(actual) || fabsf(actual-expected)>2e-5f ||
            (expected==0 && signbit(actual)!=signbit(expected))) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,rate", [(1, 1, 48000), (32, 257, 8000)])
def test_each_arrival_detects_and_interpolates_gain(tmp_path, channels, frames, rate):
    energy, gain_db = 0.0, 0.0
    gains, pcm = [], []

    def db(value):
        return -120.0 if value <= f32(1e-12) else f32(10 * f32(math.log10(value)))

    def coefficient(ms):
        denominator = f32(f32(ms * f32(.001)) * rate)
        return f32(1 - f32(math.exp(f32(-frames / denominator))))

    for call, amplitude in enumerate(map(f32, [.01, .8, 0, .03])):
        samples = [[f32((((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125 * amplitude)
                    for c in range(channels)] for n in range(frames)]
        raw = 0.0
        for row in samples:
            for value in row:
                raw = f32(raw + f32(value * value))
        raw = f32(raw / (frames * channels))
        if energy <= f32(1e-12) < raw:
            energy = raw
        else:
            c = coefficient(5.0 if raw > energy else 20.0)
            energy = f32(energy + f32(c * f32(raw - energy)))
        desired = gain_db
        if db(raw) >= -55.0:
            desired = min(12.0, max(-12.0, f32(-18.0 - db(energy))))
        old_db = gain_db
        c = coefficient(5.0 if desired < gain_db else 50.0)
        gain_db = f32(gain_db + f32(c * f32(desired - gain_db)))
        gain = f32(math.pow(10.0, f32(old_db / 20.0)))
        target = f32(math.pow(10.0, f32(gain_db / 20.0)))
        step = f32(f32(target - gain) / frames)
        for row in samples:
            gain = f32(gain + step)
            gains.append(gain)
            pcm += [int(f32(max(-1.0, min(1.0, f32(value * gain))) * 32767.0)) for value in row]
    source_code = SOURCE.replace("for(unsigned pass=0;", "static const float amplitudes[]={.01f,.8f,0,.03f};\n    for(unsigned pass=0;")
    source_code = source_code.replace("*.03125f;", "*.03125f*amplitudes[s->calls];")
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, source_code.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "ln_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)).replace(
        "@GAINS@", ",".join(map(str, gains))))
    manifests = [source, capture, *[ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                                   for name in ("loudness_normalizer", "wav_out")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", sample_rate=rate, block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "normalizer": Node("normalizer", "orpheus.builtin.loudness_normalizer", params={
            "channels": channels, "target_db": -18.0, "detector_attack_ms": 5.0,
            "detector_release_ms": 20.0, "gain_attack_ms": 5.0, "gain_release_ms": 50.0,
        }),
        "capture": Node("capture", "orpheus.test.ln_capture"),
        "sink": Node("sink", "orpheus.builtin.wav_out", params={
            "channels": channels, "sample_rate": 0, "file_path": str(tmp_path / "output.wav"),
        }),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("normalizer", "in")),
        Connection(PortRef("normalizer", "out"), PortRef("capture", "in")),
        Connection(PortRef("normalizer", "out"), PortRef("sink", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
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
                assert stream.getframerate() == rate and stream.getnchannels() == channels
                actual = struct.unpack(f"<{len(pcm)}h", stream.readframes(4 * frames))
            assert max(abs(a - b) for a, b in zip(actual, pcm)) <= 1
