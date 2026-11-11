"""音频限斜率逐次递推与运行期速率更新的四入口独立预期。"""

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_spectral_reduce_interfaces import SOURCE, f32
from test_table_interfaces import check_four_entries, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
static const float expected[4][@COUNT@]=@EXPECTED@;
int orpheus_test_slew_capture_algorithm(orpheus_test_slew_capture_State* s,
    orpheus_test_slew_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(s->calls>=4 || ctx->trigger_input || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@))
        return ORPHEUS_ERR_INVALID_ARG;
    if(memcmp(io->in.samples,expected[s->calls],sizeof(expected[0]))) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''

UPDATE = r'''
    for(int tick=0;tick<2;tick++) {
        if(tick==1) {
            OrpheusValue rate={ORPHEUS_VALUE_FLOAT};rate.value.f32=4;
#ifdef DYNAMIC
            if(runtime.set_parameter("slew","rise_rate",rate)) return 10;
            rate.value.f32=8;if(runtime.set_parameter("slew","fall_rate",rate)) return 10;
#else
            if(orpheus_control_set_value("slew","rise_rate",rate)) return 10;
            rate.value.f32=8;if(orpheus_control_set_value("slew","fall_rate",rate)) return 10;
#endif
        }
'''


def oracle(channels, frames, rise, fall, update):
    history = [0.0] * channels
    expected = []
    for call in range(4):
        rise_delta = f32((4.0 if update and call >= 2 else f32(rise)) / 48000)
        fall_delta = f32((8.0 if update and call >= 2 else f32(fall)) / 48000)
        row = []
        for frame in range(frames):
            for channel in range(channels):
                sample = f32(((frame * 17 + channel * 31) % 103 - 51) * .03125 + call * .125)
                delta = f32(sample - history[channel])
                limit = rise_delta if delta > 0 else fall_delta
                delta = limit if delta > limit else -limit if delta < -limit else delta
                history[channel] = f32(history[channel] + delta)
                row.append(history[channel])
        expected.append(row)
    return "{" + ",".join("{" + ",".join(value.hex() + "f" for value in row) + "}" for row in expected) + "}"


@pytest.mark.parametrize("channels,frames,rise,fall,update", [
    (1, 1, 1.0, 2.0, True), (3, 17, .0001, 100.0, True),
    (32, 128, 100.0, .0001, True), (2, 7, .3, .7, False),
])
def test_noise_slew_four_entries(tmp_path, channels, frames, rise, fall, update):
    source = create_component(tmp_path, "slew_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": "param:channels", "drive_port_no": 1},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
    }, SOURCE.replace("reduce_source", "slew_source"), source=True)
    capture_source = CAPTURE.replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames))
    capture_source = capture_source.replace("@EXPECTED@", oracle(channels, frames, rise, fall, update))
    capture = create_component(tmp_path, "slew_capture", {
        "in": {"direction": "input", "type": "audio", "sample_format": "f32",
               "channels": "param:channels", "process": True},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, capture_source)
    manifest = ROOT / "components" / "orpheus" / "builtin" / "noise_slew" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.slew_source"),
        "slew": Node("slew", "orpheus.builtin.noise_slew", params={
            "channels": channels, "rise_rate": rise, "fall_rate": fall,
        }),
        "capture": Node("capture", "orpheus.test.slew_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("slew", "in")),
        Connection(PortRef("slew", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    if update:
        harness = harness.replace("for(int tick=0;tick<2;tick++) {", UPDATE)
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
