"""通道拆分/合并按每次实际交付执行，不添加会合等待或同拍去重。"""

import copy

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_channel_source_algorithm(orpheus_test_channel_source_State* s,
    orpheus_test_channel_source_IO* io, const OrpheusProcessContext* ctx) {
    for(uint32_t f=0;f<ctx->frame_count;f++)
        for(int32_t ch=0;ch<s->channels;ch++)
            io->out.samples[(size_t)f*s->channels+ch]=(float)(100*f+ch+1);
    return orpheus_test_channel_source_emit_out(ctx,&io->out);
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_channel_capture_algorithm(orpheus_test_channel_capture_State* s,
    orpheus_test_channel_capture_IO* io, const OrpheusProcessContext* ctx) {
    int active=0, trigger=-1;
    for(int32_t ch=0;ch<s->channels;ch++) if(s->mask&(1<<ch)) active++;
    int step=s->calls%active;
    for(int32_t ch=0;ch<s->channels;ch++)
        if((s->mask&(1<<ch)) && step--==0) { trigger=ch; break; }
    float first=0;
    for(uint32_t f=0;f<ctx->frame_count;f++) {
        for(int32_t ch=0;ch<s->channels;ch++) {
            float expected=(s->mask&(1<<ch)) && (ctx->frame_index>0 || ch<=trigger)
                ? (float)(100*f+ch+1) : 0;
            float actual=io->in.samples[(size_t)f*s->channels+ch];
            if(actual!=expected) return ORPHEUS_ERR_UNSUPPORTED;
            s->total+=actual;
            if(!f) first+=actual;
        }
    }
    s->trace=s->trace*10+(int32_t)first;
    s->calls++;
    s->last=first;
    s->frame=(int32_t)ctx->frame_index;
    s->epoch=(int32_t)ctx->epoch;
    return ORPHEUS_OK;
}
'''


@pytest.fixture(scope="module", params=[1, 3, "sparse"], ids=["mono", "three", "unconnected"])
def channel_backends(tmp_path_factory, request):
    directory = tmp_path_factory.mktemp("channel_interface_routing")
    channels = request.param if isinstance(request.param, int) else 3
    mask = 5 if request.param == "sparse" else (1 << channels) - 1
    channel_parameter = {**scalar("int", 2), "range": [1, 64],
                         "affects_signature": True, "update_policy": "restart_required"}
    source = create_component(directory, "channel_source", {
        "out": audio("output", channels="param:channels", drive_port_no=1),
        "channels": channel_parameter, "mode": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(directory, "channel_capture", {
        "in": audio("input", channels="param:channels", process=True),
        "channels": channel_parameter, "mask": scalar("int", mask),
        "trace": scalar("int", 0), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "total": scalar("float", 0.0),
        "frame": scalar("int", 0), "epoch": scalar("int", 0),
    }, CAPTURE)
    manifests = [source, capture, *[
        ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
        for name in ("deinterleave", "interleave")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={
            "source": Node("source", "orpheus.test.channel_source", params={"channels": channels}),
            "split": Node("split", "orpheus.builtin.deinterleave", params={"channels": channels}),
            "join": Node("join", "orpheus.builtin.interleave", params={"channels": channels}),
            "capture": Node("capture", "orpheus.test.channel_capture", params={"channels": channels}),
        },
        connections=[
            Connection(PortRef("source", "out"), PortRef("split", "in")),
            *[Connection(PortRef("split", f"out{ch}"), PortRef("join", f"in{ch}"))
              for ch in range(channels) if mask & (1 << ch)],
            Connection(PortRef("join", "out"), PortRef("capture", "in")),
        ],
    ))
    return channels, mask, compile_delivery_backends(directory, registry, project, manifests)


def test_channel_samples_and_each_actual_call_match_oracle(channel_backends):
    channels, mask, backends = channel_backends
    present, trace, calls, total = set(), 0, 0, 0
    expected = []
    for tick in range(2):
        for ch in range(channels):
            if not mask & (1 << ch):
                continue
            present.add(ch)
            first = sum(lane + 1 for lane in present)
            trace = trace * 10 + first
            calls += 1
            total += sum(100 * frame + lane + 1 for frame in range(8) for lane in present)
        expected.append([tick, 0, trace, calls, first, total, tick * 8, 1])
    for executable in ("dynamic", "generated_backend"):
        result, rows = run_backend(backends, executable)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == expected


def test_channel_call_graph_contains_no_join_wait_or_dedup(channel_backends):
    channels, mask, (_, _, plan, _, _) = channel_backends
    assert [(edge["output_port"], edge["input_port"]) for edge in plan.call_graph["split"]] == [
        (ch + 1, ch) for ch in range(channels) if mask & (1 << ch)]
    assert len(plan.call_graph["join"]) == 1


@pytest.mark.parametrize("channels", [0, 65, True, "2", 2.5])
def test_channel_count_cannot_escape_declared_signature(channel_backends, channels):
    _, _, (_, _, _, project, registry) = channel_backends
    project = copy.deepcopy(project)
    project.graph.nodes["split"].params["channels"] = channels
    with pytest.raises(CompileError, match="数量"):
        GraphCompiler(registry).compile(project)
