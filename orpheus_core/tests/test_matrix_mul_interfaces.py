"""矩阵混音每次真实到达的逐项f32累计与四入口精确位型。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_matrix_capture_algorithm(orpheus_test_matrix_capture_State* s,
    orpheus_test_matrix_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[]={@EXPECTED@};
    if(ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1 ||
        ctx->frame_index!=(uint64_t)(s->calls/2*8) ||
        memcmp(io->in.samples,expected+s->calls*8*@ROWS@,8*@ROWS@*4)) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=io->in.samples[8*@ROWS@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("rows,cols,text", [
    (1, 1, None), (3, 2, "1,-0.5"), (2, 3, ""),
    (32, 32, ",".join(str(f32(((i * 17) % 97 - 48) / 103)) for i in range(1024))),
])
def test_matrix_each_arrival_matches_independent_f32_oracle(tmp_path, rows, cols, text):
    coefficients = ([1.0, 0.0, 0.0, 1.0] if text is None else
                    [f32(float(v)) for v in text.split(",")] if text else
                    [float(r == c) for r in range(rows) for c in range(cols)])
    coefficients += [0.0] * max(0, rows * cols - len(coefficients))
    expected = []
    for n in range(32):
        frame = [((n * 17 + c * 31) % 103 - 51) * .03125 for c in range(cols)]
        for r in range(rows):
            total = 0.0
            for c in range(cols):
                total = f32(total + f32(coefficients[r * cols + c] * frame[c]))
            expected.append(total)
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=cols, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(cols)).replace("@FRAMES@", "8"), source=True)
    capture = create_component(tmp_path, "matrix_capture", {
        "in": audio("input", channels=rows, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@ROWS@", str(rows)).replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected)))
    matrix = ROOT / "components" / "orpheus" / "builtin" / "matrix_mul" / "component.yaml"
    manifests = [source, matrix, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"rows": rows, "cols": cols}
    if text is not None:
        params["matrix"] = text
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "matrix": Node("matrix", "orpheus.builtin.matrix_mul", params=params),
        "capture": Node("capture", "orpheus.test.matrix_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("matrix", "in")),
        Connection(PortRef("matrix", "out"), PortRef("capture", "in")),
    ]))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    check_four_entries(case, [[0, 2, expected[16 * rows - 1], 0], [1, 4, expected[-1], 8]])
