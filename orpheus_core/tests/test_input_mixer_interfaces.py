"""固定32项行步长、输出端增益和1024项双缓冲权重切换四入口验证。"""
import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, ControlConnection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_input_matrix_source_algorithm(orpheus_test_input_matrix_source_State* s,
    orpheus_test_input_matrix_source_IO* io,const OrpheusProcessContext* ctx) {
    const float gains[]={0,-6,24,-96};
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++)
            io->out.samples[n*@CHANNELS@+c]=(float)((int)(((s->calls*@FRAMES@+n)*17+c*31)%103)-51)*.03125f;
        s->gain=gains[s->calls];
        int rc=orpheus_test_input_matrix_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''

ACCESS = r'''
#ifdef DYNAMIC
#define WRITE(v,n) runtime.write_bulk("matrix","weights",v,n)
#define WRITE_ID(v,n) runtime.write_bulk_id(@WEIGHTS_ID@,v,n)
#define READ(v) runtime.get_bulk("matrix","weights",v,1024)
#else
#define WRITE(v,n) orpheus_control_write_bulk("matrix","weights",v,n)
#define WRITE_ID(v,n) orpheus_control_write_bulk_id(@WEIGHTS_ID@,v,n)
#define READ(v) orpheus_control_get_bulk("matrix","weights",v,1024)
#endif
'''

UPDATE = r'''
        if(tick==1) {
            float previous[1024],next[1025]={0},actual[1024];
            if(READ(previous)) return 8;
            for(unsigned k=0;k<1024;k++) next[k]=(float)((int)((k*13)%31)-15)*.03125f;
            int rc=atoi(argv[argc-1]) ? WRITE_ID(next,1024) : WRITE(next,1024);
            if(rc) return 9;
            if(WRITE_ID(next,1)) return 10;
            if(!WRITE_ID(next,1025)) return 11;
            if(READ(actual) || memcmp(previous,actual,sizeof(actual))) return 12;
        }
'''


@pytest.mark.parametrize("cols,rows,frames,initial", [
    (1, 1, 1, ""), (3, 2, 7, "custom"), (32, 32, 1, "custom"), (8, 2, 5, None),
])
def test_fixed_stride_matrix_and_bulk_switch(tmp_path, cols, rows, frames, initial):
    compact = ([((k * 7) % 19 - 9) * .0625 for k in range(rows * cols)] if initial == "custom"
               else [1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0] if initial is None else [])
    weights = [[float(r == c) for c in range(cols)] for r in range(rows)]
    for k, value in enumerate(compact[:rows * cols]):
        weights[k // cols][k % cols] = value
    expected = []
    for call, db in enumerate((0, -6, 24, -96)):
        gain = f32(10 ** f32(db / 20))
        for n in range(frames):
            for r in range(rows):
                value = 0.0
                for c in range(cols):
                    coefficient = (weights[r][c] if call < 2 else
                                   (((r * 32 + c) * 13) % 31 - 15) * .03125)
                    sample = (((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125
                    value = f32(value + f32(coefficient * sample))
                expected.append(f32(value * gain))
    source = create_component(tmp_path, "input_matrix_source", {
        "out": audio("output", channels=cols, drive_port_no=1), "calls": scalar("int", 0),
        "gain": {**scalar("float", 0.0), "control_source": True},
    }, SOURCE.replace("@CHANNELS@", str(cols)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=rows, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(rows * frames)))
    matrix = ROOT / "components" / "orpheus" / "builtin" / "input_mixer_3d" / "component.yaml"
    manifests = [source, matrix, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"input_channels": cols, "output_channels": rows}
    if initial is not None:
        params["weights"] = ",".join(format(value, ".9g") for value in compact)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.input_matrix_source"),
        "matrix": Node("matrix", "orpheus.builtin.input_mixer_3d", params=params),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("matrix", "in")),
        Connection(PortRef("matrix", "out"), PortRef("capture", "in")),
    ]), control_connections=[ControlConnection(PortRef("source", "gain"), PortRef("matrix", "gain_db"))])
    plan = GraphCompiler(registry).compile(project)
    slot = next(point for point in plan.id_map if point["node"] == "matrix" and point["key"] == "weights")
    assert slot["count"] == 1024 and slot["form"] == "BULK"
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = ("#include <string.h>\n" + harness).replace("int main(",
        ACCESS.replace("@WEIGHTS_ID@", str(slot["id"])) + "\nint main(")
    harness = harness.replace("for(int tick=0;tick<2;tick++) {", "for(int tick=0;tick<2;tick++) {" + UPDATE)
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [
        [0, 2, expected[2 * frames * rows - 1], 0], [1, 4, expected[-1], frames],
    ])
