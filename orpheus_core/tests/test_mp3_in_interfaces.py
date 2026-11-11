"""准备期解码源：对照miniaudio整文件API，验证真实交付与独立PCM预期。"""
import os
import shutil
import struct
import subprocess

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.compiler import GraphCompiler
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_mp3_capture_algorithm(orpheus_test_mp3_capture_State* s,
    orpheus_test_mp3_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(ctx->trigger_input==1) {
        if(s->ticks+1!=s->calls) return ORPHEUS_ERR_PROCESSING;
        s->ticks++;return ORPHEUS_OK;
    }
    static const uint32_t expected[]={@EXPECTED@};
    if(ctx->trigger_input!=0 || s->calls>=2 || ctx->frame_count!=7 || ctx->valid_frames!=7 ||
        ctx->epoch!=1 || ctx->frame_index!=(uint64_t)s->calls*7 || ctx->sample_rate!=@RATE@ ||
        !io->in.buffer || io->in.buffer->frame_count!=7) return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<7*@CHANNELS@;n++) {
        uint32_t bits;memcpy(&bits,io->in.samples+n,4);
        if(bits!=expected[s->calls*7*@CHANNELS@+n]) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''

CHECK = r'''
        OrpheusValue value={ORPHEUS_VALUE_INT,{0}};
#ifdef DYNAMIC
        if(runtime.get_parameter("source","total_frames",&value) || value.type!=ORPHEUS_VALUE_INT ||
            value.value.i32!=@TOTAL@ || runtime.read_id(@ID@,&value) || value.value.i32!=@TOTAL@) return 8;
        if(runtime.get_parameter("capture","ticks",&value) || value.value.i32!=tick+1) return 9;
#else
        if(orpheus_control_get_value("source","total_frames",&value) || value.type!=ORPHEUS_VALUE_INT ||
            value.value.i32!=@TOTAL@ || orpheus_control_get_value_id(@ID@,&value) ||
            value.value.i32!=@TOTAL@) return 8;
        if(orpheus_control_get_value("capture","ticks",&value) || value.value.i32!=tick+1) return 9;
#endif
'''


@pytest.mark.parametrize("channels,rate,pcm", [(1, 48000, False), (2, 32000, False),
                                             (32, 48000, False), (2, 48000, True)])
def test_mp3_actual_decode_arrival_tick_and_tail(tmp_path, channels, rate, pcm):
    source_path = ROOT / "examples" / "test_input.mp3"
    if pcm:
        source_path = tmp_path / "input.wav"
        values = [((n * 17 + c * 3) % 31 - 15) * 1024 for n in range(9) for c in range(2)]
        raw = struct.pack("<" + "h" * len(values), *values)
        source_path.write_bytes(b"RIFF" + struct.pack("<I", len(raw) + 36) + b"WAVEfmt " +
                               struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 4, 4, 16) +
                               b"data" + struct.pack("<I", len(raw)) + raw)
    oracle = ROOT / "build" / "tests" / ("mp3_in_smoke.exe" if os.name == "nt" else "mp3_in_smoke")
    reference = tmp_path / "decoded.f32"
    result = subprocess.run([str(oracle), "--decode", str(source_path), str(channels), str(rate), str(reference)],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    decoded = reference.read_bytes()
    total = len(decoded) // (4 * channels)
    if pcm:
        assert total == 9
        expected = [struct.unpack("<I", struct.pack("<f", v / 32768))[0] for v in values]
        assert list(struct.unpack("<" + "I" * (len(decoded) // 4), decoded)) == expected
    else:
        assert total > 14
    path = tmp_path / ("音频输入.wav" if pcm else "音频输入.mp3")
    shutil.copyfile(source_path, path)
    prefix = decoded[:14 * channels * 4].ljust(14 * channels * 4, b"\0")
    expected = struct.unpack("<" + "I" * (14 * channels), prefix)
    code = CAPTURE.replace("@CHANNELS@", str(channels)).replace("@RATE@", str(rate))
    code = code.replace("@EXPECTED@", ",".join(str(word) + "u" for word in expected))
    capture = create_component(tmp_path, "mp3_capture", {
        "in": audio("input", channels=channels, process=True),
        "tick": {"direction": "input", "type": "trigger", "process": True},
        "calls": scalar("int", 0), "ticks": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code)
    source = ROOT / "components" / "orpheus" / "builtin" / "mp3_in" / "component.yaml"
    manifests = [source, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=7, sample_rate=rate, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.mp3_in", params={"channels": channels, "file_path": str(path)}),
        "capture": Node("capture", "orpheus.test.mp3_capture"),
    }, connections=[Connection(PortRef("source", "out"), PortRef("capture", "in")),
                    Connection(PortRef("source", "tick"), PortRef("capture", "tick"))]))
    harness = four_entry_harness().replace(",8)", ",7)").replace("(8)", "(7)")
    plan = GraphCompiler(registry).compile(project)
    point = next(point for point in plan.id_map if point["node"] == "source" and point["key"] == "total_frames")
    check = CHECK.replace("@TOTAL@", str(total)).replace("@ID@", str(point["id"]))
    harness = harness.replace('        printf("%d', check + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 1, 0, 0], [1, 2, 0, 7]])
