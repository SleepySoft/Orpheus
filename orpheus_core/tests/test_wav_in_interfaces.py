"""WAV根源：三种格式、真实RIFF块、逐位映射及尾部补零的四入口验证。"""
import struct

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_wav_capture_algorithm(orpheus_test_wav_capture_State* s,
    orpheus_test_wav_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(s->calls>=2 || ctx->trigger_input!=0 || ctx->frame_count!=7 || ctx->valid_frames!=7 ||
       ctx->sample_rate!=48000 || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)s->calls*7 ||
       !io->in.buffer || io->in.buffer->frame_count!=7) return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<7*@CHANNELS@;n++) {
        uint32_t bits;
        memcpy(&bits,io->in.samples+n,4);
        if(bits!=expected[s->calls*7*@CHANNELS@+n]) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("format,bits,file_channels,channels", [(1, 16, 2, 64), (1, 32, 4, 1), (3, 32, 3, 4)])
def test_root_emits_decoded_bits_and_zero_padding(tmp_path, format, bits, file_channels, channels):
    rows, raw = [], bytearray()
    floating = [0x80000000, 0x7FC12345, 0x7F800000, 0xFF800000, 0x3F000000, 1]
    for n in range(9):
        row = []
        for c in range(file_channels):
            if format == 3:
                word = floating[(n * file_channels + c) % len(floating)]
                raw.extend(struct.pack("<I", word))
            else:
                value = ((n * 17399991 + c * 6553711) % (1 << bits)) - (1 << (bits - 1))
                raw.extend(struct.pack("<h" if bits == 16 else "<i", value))
                word = struct.unpack("<I", struct.pack("<f", value / (1 << (bits - 1))))[0]
            row.append(word)
        rows.append(row)

    def chunk(name, data):
        return name + struct.pack("<I", len(data)) + data + (b"\0" if len(data) % 2 else b"")

    rate = 44100 if bits == 32 and format == 1 else 48000
    alignment = file_channels * bits // 8
    fmt = struct.pack("<HHIIHH", format, file_channels, rate, rate * alignment, alignment, bits)
    if format == 3:
        fmt += b"\0\0"
    chunks = chunk(b"JUNK", b"abc") + chunk(b"fmt ", fmt) + chunk(b"LIST", b"meta") + chunk(b"data", bytes(raw))
    path = tmp_path / "音频_映射.wav"
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)
    expected = []
    for n in range(14):
        for c in range(channels):
            expected.append(rows[n][c if c < file_channels else 0] if n < len(rows) else 0)
    code = CAPTURE.replace("@CHANNELS@", str(channels)).replace(
        "@EXPECTED@", ",".join(f"0x{value:08x}u" for value in expected))
    capture = create_component(tmp_path, "wav_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code)
    source = ROOT / "components" / "orpheus" / "builtin" / "wav_in" / "component.yaml"
    manifests = [source, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=7, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.wav_in", params={"channels": channels, "file_path": str(path)}),
        "capture": Node("capture", "orpheus.test.wav_capture"),
    }, connections=[Connection(PortRef("source", "out"), PortRef("capture", "in"))]))
    harness = four_entry_harness().replace(",8)", ",7)").replace("(8)", "(7)")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 1, 0, 0], [1, 2, 0, 7]])
