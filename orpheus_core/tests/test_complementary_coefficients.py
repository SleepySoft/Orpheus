"""互补通道按真实调用逐次展开，原值复制与一次 f32 减法独立验收。"""

import struct

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_complement_source_algorithm(orpheus_test_complement_source_State* s,
    orpheus_test_complement_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        int32_t call=s->calls++;
        for(int32_t i=0;i<s->channels*s->elements;i++)
            io->out.samples[i]=((i*17+call*13)%129-64)*.03125f;
        int rc=orpheus_test_complement_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_complement_capture_algorithm(orpheus_test_complement_capture_State* s,
    orpheus_test_complement_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[4][@COUNT@]={@EXPECTED@};
    if(s->calls>=4 || ctx->trigger_input || ctx->frame_count!=@STEP@ || ctx->valid_frames!=@STEP@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/2*@STEP@))
        return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned i=0;i<@COUNT@;i++) {
        uint32_t actual;
        memcpy(&actual,io->in.samples+i,4);
        if(actual!=expected[s->calls][i]) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def verify_complementary_coefficients(tmp_path, *, channels=2, elements=129, step=128, total=1.0, reference=None):
    source = create_component(tmp_path, "complement_source", {
        "out": {"direction": "output", "type": "bulk", "sample_format": "f32",
                "channels": "param:channels", "block_size": "param:elements", "drive_port_no": 1},
        "channels": parameter(channels, 32), "elements": parameter(elements, 65536), "calls": scalar("int", 0),
    }, SOURCE, source=True)
    expected = []
    for call in range(4):
        values = [((i * 17 + call * 13) % 129 - 64) * .03125 for i in range(channels * elements)]
        if reference:
            data = reference(struct.pack(f"<{len(values)}f", *values))
            expected.append(list(struct.unpack(f"<{len(data)//4}I", data)))
        else:
            row = []
            for index in range(elements):
                samples = values[index * channels:(index + 1) * channels]
                row.extend(bits(value) for value in samples)
                row.extend(bits(f32(f32(total) - value)) for value in samples)
            expected.append(row)
    count = channels * elements * 2
    capture = create_component(tmp_path, "complement_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "f32",
               "channels": channels * 2, "block_size": elements, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@COUNT@", str(count)).replace("@STEP@", str(step)).replace(
        "@EXPECTED@", ",".join("{" + ",".join(map(str, row)) + "}" for row in expected)))
    manifest = ROOT / "components" / "orpheus" / "builtin" / "complementary_coefficients" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=step, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.complement_source"),
        "complement": Node("complement", "orpheus.builtin.complementary_coefficients",
                           params={"channels": channels, "elements": elements, "total": total}),
        "capture": Node("capture", "orpheus.test.complement_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("complement", "in")),
        Connection(PortRef("complement", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{step})").replace("(8)", f"({step})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert all(config["frames"] == step for config in case[2].node_configs.values())
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, step]])


@pytest.mark.parametrize("channels,elements,step,total", [
    (2, 129, 128, 1.0), (2, 387, 384, 1.0), (1, 7, 32, -0.5), (32, 13, 8, -0.25),
])
def test_complementary_coefficients_four_entries(tmp_path, channels, elements, step, total):
    verify_complementary_coefficients(tmp_path, channels=channels, elements=elements, step=step, total=total)
