"""交付时间不受静态Task节拍或载荷长度推断约束。"""
import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.compiler import GraphCompiler
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


@pytest.mark.parametrize("frames,divisor,payload,missing", [(8, 2, 5, False), (7, 3, 14, False), (7, 3, 14, True)])
def test_real_delivery_owns_fractional_rate_and_large_payload_timeline(
    tmp_path, monkeypatch, frames, divisor, payload, missing,
):
    source = create_component(tmp_path, "rate_source", {
        "clock": audio("output", drive_port_no=1),
        "out": audio("output", block_size=payload, sample_rate=f"task:sample_rate/{divisor}", drive_port_no=2),
        "calls": scalar("int", 0),
    }, r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_rate_source_algorithm(orpheus_test_rate_source_State* s,
    orpheus_test_rate_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned n=0;n<@PAYLOAD@;++n) io->out.samples[n]=(float)(s->calls*@PAYLOAD@+n);
    io->out.delivery.frame_index=100+s->calls*@PAYLOAD@;
    ++s->calls;
    return orpheus_test_rate_source_emit_out(ctx,&io->out);
}
'''.replace("@PAYLOAD@", str(payload)), source=True)
    capture = create_component(tmp_path, "rate_capture", {
        "in": audio("input", block_size=payload, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0), "frame": scalar("int", 0),
    }, r'''
#include "component_state.h"
int orpheus_test_rate_capture_algorithm(orpheus_test_rate_capture_State* s,
    orpheus_test_rate_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
       ctx->sample_rate!=48000.0/@DIVISOR@ || ctx->frame_index!=100+(uint64_t)s->calls*@PAYLOAD@ ||
       !ctx->delivery || io->in.buffer->frame_count!=@PAYLOAD@ ||
       ctx->timestamp!=(double)ctx->frame_index/ctx->sample_rate)
        return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<@PAYLOAD@;++n)
        if(io->in.samples[n]!=(float)(s->calls*@PAYLOAD@+n)) return ORPHEUS_ERR_PROCESSING;
    ++s->calls;s->last=io->in.samples[@PAYLOAD@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''.replace("@FRAMES@", str(frames)).replace("@DIVISOR@", str(divisor)).replace("@PAYLOAD@", str(payload)))
    registry = Registry()
    for path in (source, capture):
        registry.add_manifest(path)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.rate_source"),
        "capture": Node("capture", "orpheus.test.rate_capture"),
    }, connections=[Connection(PortRef("source", "out"), PortRef("capture", "in"))]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    if missing:
        harness = harness.replace("return 3;", """{
#ifndef DYNAMIC
            const OrpheusGraphError* error=orpheus_graph_last_error();
            fprintf(stderr,"%s %s\\n",error->operation,error->node);
#endif
            return 3;
        }""")
        compile_graph = GraphCompiler.compile

        def direct_consumer_entry(compiler, *args, **kwargs):
            plan = compile_graph(compiler, *args, **kwargs)
            plan.entry_nodes = ["capture"]
            for task in plan.tasks:
                task["entry_nodes"] = ["capture"]
            return plan

        monkeypatch.setattr(GraphCompiler, "compile", direct_consumer_entry)
    case = compile_delivery_backends(tmp_path, registry, project, [source, capture], harness)
    assert case[2].node_configs["capture"]["timeline_from_delivery"] is True
    assert "frame_origin" not in case[2].node_configs["capture"]
    assert "frame_step" not in case[2].node_configs["capture"]
    if missing:
        for backend in ("dynamic", "generated_backend"):
            for mode in (0, 1):
                result, rows = run_backend(case, backend, mode=mode)
                assert result.returncode != 0 and not rows
                assert "capture" in result.stdout + result.stderr
    else:
        check_four_entries(case, [[0, 1, payload - 1, 100], [1, 2, 2 * payload - 1, 100 + payload]])
