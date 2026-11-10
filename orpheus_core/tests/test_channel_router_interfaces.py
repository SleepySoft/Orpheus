"""通道路由在真实四入口保留原始位型和每次交付，不依赖线性重写。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_bulk_arithmetic_interfaces import HARNESS
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar

SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_router_source_algorithm(orpheus_test_router_source_State* s,
    orpheus_test_router_source_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t bits[]={0x7fa12345,0x80000000,0x7fc76543,1,0x7f800000,0xff800000,0x3f800000};
    for(unsigned phase=0;phase<2;phase++) {
        for(uint32_t n=0;n<ctx->frame_count;n++)
            for(uint32_t c=0;c<io->out.buffer->channels;c++)
                memcpy(io->out.samples+n*io->out.buffer->channels+c,&bits[(n+c+phase+s->ticks)%7],4);
        int rc=orpheus_test_router_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    s->ticks++;
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_router_capture_algorithm(orpheus_test_router_capture_State* s,
    orpheus_test_router_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t bits[]={0x7fa12345,0x80000000,0x7fc76543,1,0x7f800000,0xff800000,0x3f800000};
    static const int32_t map[]={@MAP@};
    if(ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/2*8)) return ORPHEUS_ERR_INVALID_ARG;
    for(uint32_t n=0;n<8;n++)
        for(uint32_t c=0;c<@COUNT@;c++) {
            uint32_t actual,expected=map[c]>=0 && map[c]<@INPUT@
                ? bits[(n+(uint32_t)map[c]+s->calls%2+s->calls/2)%7] : 0;
            memcpy(&actual,io->in.samples+n*@COUNT@+c,4);
            if(actual!=expected) return ORPHEUS_ERR_PROCESSING;
        }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,count", [(1, 7), (64, 64)])
def test_channel_router_four_entries_preserve_each_delivery(tmp_path, channels, count):
    mapping = ([0, -1, channels - 1, channels, 2147483647, -2147483648, 0, 0] * 8)[:count]
    source = create_component(tmp_path, "router_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "ticks": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(tmp_path, "router_capture", {
        "in": audio("input", channels=count, process=True),
        "calls": scalar("int", 0), "frame": scalar("int", 0), "last": scalar("float", 0.0),
    }, CAPTURE.replace("@MAP@", ",".join(map(str, mapping)))
        .replace("@INPUT@", str(channels)).replace("@COUNT@", str(count)))
    router = ROOT / "components" / "orpheus" / "builtin" / "channel_router" / "component.yaml"
    manifests = [source, router, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.router_source"),
        "router": Node("router", "orpheus.builtin.channel_router",
                       params={"channels_in": channels, "channels_out": count, "indices": ",".join(map(str, mapping))}),
        "capture": Node("capture", "orpheus.test.router_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("router", "in")),
        Connection(PortRef("router", "out"), PortRef("capture", "in")),
    ]))
    harness = "#include <stdlib.h>\n" + HARNESS.replace(
        'runtime.process_task("task_source",8)',
        '(atoi(argv[argc-1]) ? runtime.process_task("task_source",8) : runtime.process_block(8))',
    ).replace("orpheus_generated_process_task_task_source(8)",
              "(atoi(argv[argc-1]) ? orpheus_generated_process_task_task_source(8) : orpheus_generated_process(8))")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            result, rows = run_backend(case, backend, mode=mode)
            assert result.returncode == 0, result.stdout + result.stderr
            assert rows == [[0, 2, 0, 0], [1, 4, 0, 8]]
