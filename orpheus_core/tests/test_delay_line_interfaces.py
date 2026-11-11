"""逐通道延迟的重复到达、位型与绝对量子四入口数值验收。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_delay_source_algorithm(orpheus_test_delay_source_State* s,
    orpheus_test_delay_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++)
            for(unsigned c=0;c<@CHANNELS@;c++)
                io->out.samples[n*@CHANNELS@+c]=
                    (float)((int)(((s->calls*@FRAMES@+n)*17+c*31)%103)-51)*.03125f;
        int rc=orpheus_test_delay_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''
CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_delay_capture_algorithm(orpheus_test_delay_capture_State* s,
    orpheus_test_delay_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
        ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@) ||
        memcmp(io->in.samples,expected+s->calls*@COUNT@,@COUNT@*4)) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=io->in.samples[@COUNT@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,maximum,integer", [
    (1, 1, 3, False), (3, 7, 16, False), (3, 8, 192000, True), (64, 257, 65536, True),
])
def test_delay_each_arrival_has_independent_history_not_task_dedup(tmp_path, channels, frames, maximum, integer):
    delays = [(0 if c % 3 == 0 else maximum if c % 3 == 2 else 1) if integer else c * 2 + .5
              for c in range(channels)]
    effective = [int(value) if integer else int(value + .5) for value in delays]
    samples = [((n * 17 + c * 31) % 103 - 51) * .03125
               for n in range(4 * frames) for c in range(channels)]
    expected = [samples[(n - effective[c]) * channels + c] if n >= effective[c] else 0.0
                for n in range(4 * frames) for c in range(channels)]
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "delay_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    delay = ROOT / "components" / "orpheus" / "builtin" / "delay_line" / "component.yaml"
    manifests = [source, delay, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"channels": channels, "max_delay_samples": maximum, "delay_vector_mode": int(integer)}
    params["initial_delays_i32" if integer else "delays_samples"] = ",".join(str(v) for v in delays)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "delay": Node("delay", "orpheus.builtin.delay_line", params=params),
        "capture": Node("capture", "orpheus.test.delay_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("delay", "in")),
        Connection(PortRef("delay", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("    return 0;\n}", "#ifndef DYNAMIC\n"
                              "    orpheus_generated_teardown();\n#endif\n    return 0;\n}")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
