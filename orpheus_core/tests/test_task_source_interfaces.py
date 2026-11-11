"""纯Task源只交付编号触发口，分频由组件处理，不分配PCM。"""

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project, ProjectLoader
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
int orpheus_test_task_capture_algorithm(orpheus_test_task_capture_State* s,
    orpheus_test_task_capture_IO* io,const OrpheusProcessContext* ctx) {
    (void)io;
    if(ctx->frame_count!=@QUANTUM@ || ctx->valid_frames!=@QUANTUM@ || ctx->epoch!=1 ||
       !ctx->delivery || ctx->delivery->buffer) return ORPHEUS_ERR_PROCESSING;
    if(ctx->trigger_input==0) {
        if(ctx->frame_index!=(uint64_t)s->fast*@QUANTUM@ || s->slow!=(s->fast+1)/3)
            return ORPHEUS_ERR_PROCESSING;
        ++s->fast;
        s->last+=1;
    } else if(ctx->trigger_input==1) {
        if(ctx->frame_index!=(uint64_t)(s->slow*3+1)*@QUANTUM@ || s->fast!=s->slow*3+2)
            return ORPHEUS_ERR_PROCESSING;
        ++s->slow;
        s->last+=2;
    } else return ORPHEUS_ERR_INVALID_ARG;
    s->frame=(int32_t)ctx->frame_index;
    ++s->calls;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("frequency", [100, 1000])
def test_task_source_numbered_fanout_and_real_divider_four_entries(tmp_path, frequency):
    quantum = 48000 // frequency
    trigger = {"direction": "input", "type": "trigger", "process": True}
    capture = create_component(tmp_path, "task_capture", {
        "fast": trigger, "slow": trigger,
        "calls": scalar("int", 0), "last": scalar("float", 0), "frame": scalar("int", 0),
        "fast_calls": scalar("int", 0), "slow_calls": scalar("int", 0),
    }, CAPTURE.replace("s->fast", "s->fast_calls").replace("s->slow", "s->slow_calls")
        .replace("@QUANTUM@", str(quantum)))
    manifests = [ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                 for name in ("task_source", "bulk_relay", "trigger_divider")]
    manifests.append(capture)
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=32, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.task_source", params={"frequency_hz": frequency}),
        "relay": Node("relay", "orpheus.builtin.bulk_relay", params={"count": 1}),
        "divide": Node("divide", "orpheus.builtin.trigger_divider", params={"factor": 3, "phase": 1}),
        "capture": Node("capture", "orpheus.test.task_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("capture", "fast")),
        Connection(PortRef("source", "out"), PortRef("relay", "in")),
        Connection(PortRef("relay", "out"), PortRef("divide", "in")),
        Connection(PortRef("divide", "out"), PortRef("capture", "slow")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{quantum})").replace("(8)", f"({quantum})")
    harness = harness.replace("tick<2", "tick<6")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    plan = case[2]
    assert plan.buffers == {} and plan.tasks[0]["frequency_hz"] == frequency
    assert plan.node_configs["source"]["trigger_output_ports"] == ["out"]
    assert [edge["input_port"] for edge in plan.call_graph["source"]] == [0, 0]
    check_four_entries(case, [
        [tick, tick + 1 + (tick + 2) // 3, tick + 1 + 2 * ((tick + 2) // 3), tick * quantum]
        for tick in range(6)
    ])
    project.drive_connections = [
        DriveConnection(PortRef("source", "out"), PortRef("relay", "in"), 2, 1)
    ]
    with pytest.raises(CompileError, match="连接不能再次分频"):
        GraphCompiler(registry).compile(project)


@pytest.mark.parametrize("name", ["trigger_source_demo", "task_sequence_demo", "trigger_async_buffer_demo"])
def test_task_source_examples_use_real_interfaces(name):
    registry = Registry([ROOT / "components"])
    registry.scan()
    project = ProjectLoader(registry).load(ROOT / "examples" / (name + ".yaml"))
    plan = GraphCompiler(registry).compile(project)
    for nid, cfg in plan.node_configs.items():
        if cfg["component"] == "orpheus.builtin.task_source":
            assert cfg["output_drive_ports"] == {"out": 1}
            assert all(edge["output_port"] == 1 for edge in plan.call_graph[nid])
