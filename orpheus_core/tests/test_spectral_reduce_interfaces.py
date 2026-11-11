"""频谱归约逐次真实交付，保留短块/尾块和广播长度。"""

import struct

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_reduce_source_algorithm(orpheus_test_reduce_source_State* s,
    orpheus_test_reduce_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        int32_t call=s->calls++;
        for(uint32_t f=0;f<ctx->frame_count;f++) for(int32_t c=0;c<s->channels;c++)
            io->out.samples[f*s->channels+c]=((int)((f*17+c*31)%103)-51)*.03125f+call*.125f;
        int rc=orpheus_test_reduce_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
static const float expected[4][@CHANNELS@]=@EXPECTED@;
int orpheus_test_reduce_capture_algorithm(orpheus_test_reduce_capture_State* s,
    orpheus_test_reduce_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(s->calls>=4 || ctx->trigger_input || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@))
        return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned f=0;f<@FRAMES@;f++) for(unsigned c=0;c<@CHANNELS@;c++)
        if(memcmp(&io->in.samples[f*@CHANNELS@+c],&expected[s->calls][c],sizeof(float)))
            return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def oracle(channels, frames, fft, batches, requested_bins, operation):
    bins = requested_bins if 0 < requested_bins <= fft else fft // 2 + 1
    result = []
    for call in range(4):
        row = []
        for channel in range(channels):
            samples = [f32(((frame * 17 + channel * 31) % 103 - 51) * .03125 + call * .125)
                       for frame in range(frames) if frame // fft < batches and frame % fft < bins]
            acc = 0.0
            for sample in samples:
                acc = f32(acc + sample)
            row.append(acc if operation == "sum" else f32(acc / len(samples)) if operation == "mean"
                       else min(samples) if operation == "min" else max(samples))
        result.append(row)
    return "{" + ",".join("{" + ",".join(value.hex() + "f" for value in row) + "}" for row in result) + "}"


@pytest.mark.parametrize("channels,frames,fft,batches,bins,operation", [
    (1, 1, 2, 1, 0, "mean"),
    (3, 17, 4, 3, 2, "sum"),
    (3, 7, 4, 3, 0, "mean"),
    (64, 64, 128, 12, 4096, "min"),
    (2, 128, 64, 2, 64, "max"),
    (1, 8, 4096, 256, 4096, "sum"),
])
def test_spectral_reduce_four_entries(tmp_path, channels, frames, fft, batches, bins, operation):
    source = create_component(tmp_path, "reduce_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": "param:channels", "drive_port_no": 1},
        "channels": parameter(channels, 64), "calls": scalar("int", 0),
    }, SOURCE, source=True)
    capture_source = CAPTURE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames))
    capture_source = capture_source.replace("@EXPECTED@", oracle(channels, frames, fft, batches, bins, operation))
    capture = create_component(tmp_path, "reduce_capture", {
        "in": {"direction": "input", "type": "audio", "sample_format": "f32",
               "channels": "param:channels", "process": True},
        "channels": parameter(channels, 64), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, capture_source)
    manifest = ROOT / "components" / "orpheus" / "builtin" / "spectral_reduce" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.reduce_source"),
        "reduce": Node("reduce", "orpheus.builtin.spectral_reduce", params={
            "channels": channels, "fft_size": fft, "num_frames": batches,
            "bin_count": bins, "operation": operation,
        }),
        "capture": Node("capture", "orpheus.test.reduce_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("reduce", "in")),
        Connection(PortRef("reduce", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert all(config["frames"] == frames for config in case[2].node_configs.values())
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
