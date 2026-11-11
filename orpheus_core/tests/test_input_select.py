"""通道覆盖映射的真实四入口交付、运行期更新与位型验收。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_channel_router_interfaces import SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_selection_capture_algorithm(orpheus_test_selection_capture_State* s,
    orpheus_test_selection_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t bits[]={0x7fa12345,0x80000000,0x7fc76543,1,0x7f800000,0xff800000,0x3f800000};
    static const int32_t map[2][@COUNT@]={{@BASE@},{@UPDATED@}};
    if(ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1 ||
        ctx->frame_index!=(uint64_t)(s->calls/2*8)) return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned n=0;n<8;n++) for(unsigned c=0;c<@COUNT@;c++) {
        int32_t index=map[s->calls/2][c];
        uint32_t expected=index<0 ? 0 : bits[(n+index+s->calls%2+s->calls/2)%7],actual;
        memcpy(&actual,io->in.samples+n*@COUNT@+c,4);
        if(actual!=expected) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,count,base,override,base_map,updated_map", [
    (1, 7, "", "0,1,2,0,-1", [0, -1, -1, -1, -1, -1, -1], [0, 0, -1, -1, -1, -1, -1]),
    (4, 4, "1,2,3,4", "0,4,99,-1", [0, 1, 2, 3], [0, 3, -1, -1]),
    (3, 8, "3,1", "0,2,0,3", [2, 0, 2, -1, -1, -1, -1, -1], [2, 1, 2, 2, -1, -1, -1, -1]),
    (32, 32, ",".join(str(i) for i in range(32, 0, -1)),
     ",".join(str(i) for i in [0, 32, 1, 33, -1, 2147483647, -2147483648, 0] * 4),
     list(range(31, -1, -1)),
     [v for i in range(4) for v in [31 - i * 8, 31, 0, -1, -1, -1, -1, 24 - i * 8]]),
])
def test_override_zero_keeps_base_and_invalid_mutes_four_entries(
    tmp_path, channels, count, base, override, base_map, updated_map,
):
    source = create_component(tmp_path, "router_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "ticks": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(tmp_path, "selection_capture", {
        "in": audio("input", channels=count, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@COUNT@", str(count)).replace("@BASE@", ",".join(map(str, base_map)))
        .replace("@UPDATED@", ",".join(map(str, updated_map))))
    selector = ROOT / "components" / "orpheus" / "builtin" / "input_select" / "component.yaml"
    manifests = [source, selector, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.router_source"),
        "selector": Node("selector", "orpheus.builtin.input_select", params={
            "channels_in": channels, "channels_out": count, "select": base,
        }),
        "capture": Node("capture", "orpheus.test.selection_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("selector", "in")),
        Connection(PortRef("selector", "out"), PortRef("capture", "in")),
    ]))
    update = r'''
        if(tick) {
            OrpheusValue value={ORPHEUS_VALUE_STRING};value.value.str="@OVERRIDE@";
#ifdef DYNAMIC
            if(runtime.set_parameter("selector","override_select",value)) return 10;
#else
            if(orpheus_control_set_value("selector","override_select",value)) return 10;
#endif
        }
'''
    readback = r'''
        OrpheusValue actual={ORPHEUS_VALUE_STRING};
#ifdef DYNAMIC
        if(runtime.get_parameter("selector","override_select",&actual)) return 11;
#else
        if(orpheus_control_get_value("selector","override_select",&actual)) return 11;
#endif
        if(actual.type!=ORPHEUS_VALUE_STRING || strcmp(actual.value.str,tick ? "@OVERRIDE@" : "")) return 12;
'''
    harness = ("#include <string.h>\n" + four_entry_harness()).replace(
        "for(int tick=0;tick<2;tick++) {",
        "for(int tick=0;tick<2;tick++) {" + update.replace("@OVERRIDE@", override),
    ).replace('        printf(', readback.replace("@OVERRIDE@", override) + '        printf(')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, 8]])
