"""WAV终点四入口文件逐字节对照独立PCM量化预期。"""

import struct

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_wav_source_algorithm(orpheus_test_wav_source_State* s,
    orpheus_test_wav_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned f=0;f<ctx->frame_count;f++)
            for(unsigned c=0;c<@CHANNELS@;c++)
                io->out.samples[f*@CHANNELS@+c]=
                    (float)((int)((f*17+c*31+s->calls*7)%103)-51)*.03125f;
        int rc=orpheus_test_wav_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,rate,configured_rate", [
    (1, 1, 44100, 0), (2, 8, 48000, 0), (64, 257, 96000, 0), (3, 16, 48000, 48000),
])
def test_wav_sink_each_arrival_and_auto_rate_match_independent_pcm(tmp_path, channels, frames, rate, configured_rate):
    source = create_component(tmp_path, "wav_source", {
        "out": audio("output", channels=channels, drive_port_no=1),
        "calls": scalar("int", 0), "last": scalar("float", 0), "frame": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)), source=True)
    wav = ROOT / "components" / "orpheus" / "builtin" / "wav_out" / "component.yaml"
    output = tmp_path / "output" / "audio.wav"
    manifests = [source, wav]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, sample_rate=rate, graph=Graph(
        nodes={
            "source": Node("source", "orpheus.test.wav_source"),
            "sink": Node("sink", "orpheus.builtin.wav_out", params={
                "channels": channels, "sample_rate": configured_rate, "file_path": str(output),
            }),
        }, connections=[Connection(PortRef("source", "out"), PortRef("sink", "in"))],
    ))
    harness = four_entry_harness().replace('"capture"', '"source"')
    harness = harness.replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace(f"init(48000,{frames})", f"init({rate},{frames})")
    harness = harness.replace("    return 0;\n}", "#ifndef DYNAMIC\n"
                              "    orpheus_generated_teardown();\n#endif\n    return 0;\n}")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    samples = [f32(((frame * 17 + channel * 31 + call * 7) % 103 - 51) * .03125)
               for call in range(4) for frame in range(frames) for channel in range(channels)]
    pcm = b"".join(struct.pack("<h", int(f32(max(-1, min(1, value)) * 32767))) for value in samples)
    expected = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(pcm), b"WAVE", b"fmt ", 16,
                           1, channels, rate, rate * channels * 2, channels * 2, 16, b"data", len(pcm)) + pcm
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            output.unlink(missing_ok=True)
            result, rows = run_backend(case, backend, mode)
            assert result.returncode == 0, (backend, mode, result.stdout, result.stderr)
            assert rows == [[0, 2, 0, 0], [1, 4, 0, frames]]
            assert output.read_bytes() == expected, (backend, mode)
