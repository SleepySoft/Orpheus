"""共享系数FIR的连续历史、逐项f32卷积和每次实际交付四入口验收。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_fir_capture_algorithm(orpheus_test_fir_capture_State* s,
    orpheus_test_fir_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
        ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@) ||
        memcmp(io->in.samples,expected+s->calls*@COUNT@,@COUNT@*4)) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=io->in.samples[@COUNT@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,coefficients", [
    (1, 1, None), (3, 7, []),
    (32, 8, [f32((i % 5 - 2) / 17) for i in range(17)]),
    (3, 257, [f32((i % 19 - 9) / 103) for i in range(1024)]),
])
def test_fir_each_arrival_advances_independent_convolution_history(tmp_path, channels, frames, coefficients):
    kernel = coefficients or [1.0]
    samples = [[((n * 17 + c * 31) % 103 - 51) * .03125 for c in range(channels)]
               for n in range(4 * frames)]
    expected = []
    for n in range(4 * frames):
        for c in range(channels):
            total = 0.0
            for k in range(min(n + 1, len(kernel))):
                total = f32(total + f32(kernel[k] * samples[n - k][c]))
            expected.append(total)
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    fir = ROOT / "components" / "orpheus" / "builtin" / "fir" / "component.yaml"
    manifests = [source, fir, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"channels": channels}
    if coefficients is not None:
        params["coefficients"] = ",".join(map(str, coefficients))
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "fir": Node("fir", "orpheus.builtin.fir", params=params),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("fir", "in")),
        Connection(PortRef("fir", "out"), PortRef("capture", "in")),
    ]))
    probe = r'''
        OrpheusValue taps={ORPHEUS_VALUE_INT};
#ifdef DYNAMIC
        if(runtime.get_parameter("fir","taps",&taps)) return 10;
#else
        if(orpheus_control_get_value("fir","taps",&taps)) return 10;
#endif
        if(taps.type!=ORPHEUS_VALUE_INT || taps.value.i32!=@TAPS@) return 11;
'''
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("        printf(", probe.replace("@TAPS@", str(len(kernel))) + "        printf(")
    harness = harness.replace("    return 0;\n}", "#ifndef DYNAMIC\n"
                              "    orpheus_generated_teardown();\n#endif\n    return 0;\n}")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
