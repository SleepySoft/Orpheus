"""插值矩阵的独立f32预期、控制位置更新与每次真实交付四入口验证。"""
import math

import pytest

from orpheus_core.project import Connection, ControlConnection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_slc_source_algorithm(orpheus_test_slc_source_State* s,
    orpheus_test_slc_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@COLS@;c++)
            io->out.samples[n*@COLS@+c]=(float)((int)(((s->calls*@FRAMES@+n)*17+c*31)%103)-51)*.03125f;
        s->index=s->calls==0 ? @QUERY@f : @NEXT@f;
        int rc=orpheus_test_slc_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''


def interpolate(tables, axis, query, logarithmic):
    if len(axis) == 1 or query <= axis[0]:
        return list(tables[0])
    if query >= axis[-1]:
        return list(tables[-1])
    index = next(i for i in range(len(axis) - 1) if axis[i] <= query < axis[i + 1])
    span = f32(axis[index + 1] - axis[index])
    t = f32(f32(query - axis[index]) / span) if span > f32(1e-12) else 0.0
    result = []
    for a, b in zip(tables[index], tables[index + 1]):
        if logarithmic:
            a, b = [f32(math.log(max(value, f32(5.0118723e-7)))) for value in (a, b)]
        value = f32(f32(f32(1 - t) * a) + f32(t * b))
        result.append(f32(math.exp(value)) if logarithmic else value)
    return result


@pytest.mark.parametrize("rows,cols,table_count,logarithmic,query,next_query,coefficient,frozen,tiny", [
    (1, 1, 1, False, .25, .75, .5, False, False),
    (2, 3, 3, False, .25, .75, .5, False, False),
    (3, 2, 3, True, .25, .75, .75, False, False),
    (32, 32, 8, False, -.5, 3., 1., False, False),
    (2, 2, 3, True, .25, .75, 1e-7, True, False),
    (2, 2, 3, False, 2.5e-13, .75, 1e-7, False, True),
])
def test_interpolated_matrix_actual_arrivals(tmp_path, rows, cols, table_count, logarithmic,
                                           query, next_query, coefficient, frozen, tiny):
    frames = 3
    query, next_query, coefficient = map(f32, (query, next_query, coefficient))
    tables = [[f32(2.0 ** ((t + k) % 5 - 2) if logarithmic else ((t * 7 + k * 3) % 17 - 8) * .0625)
               for k in range(rows * cols)] for t in range(table_count)]
    if logarithmic:
        tables[0][0] = 0.0
        tables[-1][-1] = -1.0
    axis = list(map(f32, [0, 5e-13, 1] if tiny else range(table_count)))
    active = interpolate(tables, axis, query, logarithmic)
    expected = []
    for call in range(4):
        target = interpolate(tables, axis, query if call == 0 else next_query, logarithmic)
        for n in range(frames):
            if not frozen:
                active = (list(target) if coefficient <= f32(1e-6) else
                          [f32(f32(coefficient * a) + f32(f32(1 - coefficient) * t))
                           for a, t in zip(active, target)])
            for r in range(rows):
                value = 0.0
                for c in range(cols):
                    sample = (((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125
                    value = f32(value + f32(active[r * cols + c] * sample))
                expected.append(value)
    source = create_component(tmp_path, "slc_source", {
        "out": audio("output", channels=cols, drive_port_no=1), "calls": scalar("int", 0),
        "index": {**scalar("float", query), "control_source": True},
    }, SOURCE.replace("@COLS@", str(cols)).replace("@FRAMES@", str(frames))
        .replace("@QUERY@", f"{query:.9e}").replace("@NEXT@", f"{next_query:.9e}"), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=rows, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(rows * frames)))
    matrix = ROOT / "components" / "orpheus" / "builtin" / "slc_matrix_mul" / "component.yaml"
    manifests = [source, matrix, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.slc_source"),
        "matrix": Node("matrix", "orpheus.builtin.slc_matrix_mul", params={
            "rows": rows, "cols": cols, "num_tables": table_count,
            "tables": ",".join(format(value, ".9g") for table in tables for value in table),
            "interp_x": ",".join(format(value, ".9g") for value in axis),
            "interp_index": query, "interp_method": int(logarithmic),
            "ramp_coeff": coefficient, "freeze": int(frozen)}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("matrix", "in")),
        Connection(PortRef("matrix", "out"), PortRef("capture", "in")),
    ]), control_connections=[ControlConnection(PortRef("source", "index"), PortRef("matrix", "interp_index"))])
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [
        [0, 2, expected[2 * frames * rows - 1], 0], [1, 4, expected[-1], frames],
    ])
