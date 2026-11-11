"""重叠分帧逐位预期与按绝对输出位置累加的独立OLA预期。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_overlap_source_algorithm(orpheus_test_overlap_source_State* s,
    orpheus_test_overlap_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        unsigned call=s->calls++;
        for(unsigned i=0;i<@INPUT@;i++) io->out.samples[i]=((int)((i*13+call*17)%67)-33)*.03125f;
        int rc=orpheus_test_overlap_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_overlap_capture_algorithm(orpheus_test_overlap_capture_State* s,
    orpheus_test_overlap_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t windows[4][@OUTPUT@]={@WINDOWS@},sums[4][@INPUT@]={@SUMS@};
    unsigned port=s->calls%2,call=s->calls/2;
    if(s->calls>=8 || ctx->trigger_input!=port || ctx->frame_count!=7 || ctx->valid_frames!=7 ||
        ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/4*7))
        return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* input=port ? io->summed.buffer : io->windowed.buffer;
    if(input->frame_count!=(port ? @IN_FRAMES@ : @OUT_FRAMES@) ||
        memcmp(input->data,port ? sums[call] : windows[call],(port ? @INPUT@ : @OUTPUT@)*4))
        return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=((const float*)input->data)[(port ? @INPUT@ : @OUTPUT@)-1];
    s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def c_rows(values):
    return ",".join("{" + ",".join(str(bits(v)) for v in row) + "}" for row in values)


@pytest.mark.parametrize("frame,hop,batches,channels", [
    (4, 2, 3, 2), (7, 1, 2, 3), (16, 16, 1, 64),
    (2, 1, 256, 1), (4096, 4095, 1, 1),
])
def test_window_history_and_overlap_sum_four_entries(tmp_path, frame, hop, batches, channels):
    input_frames, output_frames = hop * batches, frame * batches
    inputs = [[f32(((i * 13 + call * 17) % 67 - 33) * .03125)
               for i in range(input_frames * channels)] for call in range(4)]
    history = [0.0] * ((frame - hop) * channels)
    windowed, summed = [], []
    accumulated = [0.0] * ((4 * input_frames + frame) * channels)
    for call, samples in enumerate(inputs):
        complete = history + samples
        windows = [complete[(batch * hop + n) * channels + ch]
                   for batch in range(batches) for n in range(frame) for ch in range(channels)]
        if history:
            history = complete[-len(history):]
        windowed.append(windows)
        for batch in range(batches):
            position = (call * batches + batch) * hop
            for n in range(frame):
                for ch in range(channels):
                    target = (position + n) * channels + ch
                    value = windows[(batch * frame + n) * channels + ch]
                    accumulated[target] = f32(accumulated[target] + value)
        start = call * input_frames * channels
        summed.append(accumulated[start:start + input_frames * channels])
    audio = {"type": "audio", "sample_format": "f32", "channels": channels}
    source = create_component(tmp_path, "overlap_source", {
        "out": {**audio, "direction": "output", "block_size": input_frames, "drive_port_no": 1},
        "calls": scalar("int", 0),
    }, SOURCE.replace("@INPUT@", str(input_frames * channels)), source=True)
    capture = create_component(tmp_path, "overlap_capture", {
        "windowed": {**audio, "direction": "input", "block_size": output_frames, "process": True},
        "summed": {**audio, "direction": "input", "block_size": input_frames, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@INPUT@", str(input_frames * channels))
        .replace("@OUTPUT@", str(output_frames * channels))
        .replace("@IN_FRAMES@", str(input_frames)).replace("@OUT_FRAMES@", str(output_frames))
        .replace("@WINDOWS@", c_rows(windowed)).replace("@SUMS@", c_rows(summed)))
    window = ROOT / "components" / "orpheus" / "builtin" / "circular_buffer" / "component.yaml"
    ola = ROOT / "components" / "orpheus" / "builtin" / "overlap_add" / "component.yaml"
    manifests = [source, window, ola, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"channels": channels, "frame_size": frame, "hop_size": hop, "num_frames": batches}
    project = Project(flow_mode="task_sequence", block_size=7, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.overlap_source"),
        "window": Node("window", "orpheus.builtin.circular_buffer", params=params),
        "ola": Node("ola", "orpheus.builtin.overlap_add", params=params),
        "capture": Node("capture", "orpheus.test.overlap_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("window", "in")),
        Connection(PortRef("window", "out"), PortRef("capture", "windowed")),
        Connection(PortRef("window", "out"), PortRef("ola", "in")),
        Connection(PortRef("ola", "out"), PortRef("capture", "summed")),
    ]))
    harness = four_entry_harness().replace(",8)", ",7)").replace("(8)", "(7)")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 4, summed[1][-1], 0], [1, 8, summed[3][-1], 7]])
