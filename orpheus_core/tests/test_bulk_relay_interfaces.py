"""无PCM触发链、逐次控制交付与固定容量/逻辑shape的四入口验证。"""

import pytest

from orpheus_core.project import ControlConnection, DriveConnection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import CLOCK, bits, check_four_entries, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_relay_capture_algorithm(orpheus_test_relay_capture_State* s,
    orpheus_test_relay_capture_IO* io,const OrpheusProcessContext* ctx) {
    (void)io;
    static const uint32_t expected[]={@EXPECTED@};
    if(ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1 ||
        ctx->frame_index!=(uint64_t)(s->calls/2*8) ||
        memcmp(s->values,expected,sizeof(expected))) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=s->values[s->elements-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("count,short", [(1, False), (4, False), (17, True), (2048, False)])
def test_relay_trigger_chain_preserves_bulk_control_values_without_pcm(tmp_path, count, short):
    used = count // 2 if short else count
    initial = [-0.0] + [((i * 37) % 199 - 99) * .03125 for i in range(1, used)]
    expected = initial + [0.0] * (2048 - used)
    source = create_component(tmp_path, "constant_clock", {
        "out": {"direction": "output", "type": "trigger", "drive_port_no": 1},
    }, CLOCK, source=True)
    capture = create_component(tmp_path, "relay_capture", {
        "in": {"direction": "input", "type": "trigger", "process": True},
        "elements": {"type": "int", "default": count, "range": [1, 2048],
                     "affects_signature": True, "update_policy": "restart_required"},
        "values": {"type": "float", "count": 2048, "default": [0.0] * 2048,
                   "shape": ["param:elements"], "kind": "bulk", "bindable": True,
                   "bulk": {"slot_key": "values_storage", "double_bank": True}},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected)))
    relay = ROOT / "components" / "orpheus" / "builtin" / "bulk_relay" / "component.yaml"
    manifests = [source, relay, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.constant_clock"),
        "a": Node("a", "orpheus.builtin.bulk_relay", params={
            "count": count, "initial_values": ",".join(format(v, ".9g") for v in initial)}),
        "b": Node("b", "orpheus.builtin.bulk_relay", params={"count": count}),
        "capture": Node("capture", "orpheus.test.relay_capture"),
    }), drive_connections=[
        DriveConnection(PortRef(src, "out"), PortRef(dst, "in"))
        for src, dst in (("source", "a"), ("a", "b"), ("b", "capture"))
    ], control_connections=[
        ControlConnection(PortRef(src, "values"), PortRef(dst, "values"))
        for src, dst in (("a", "b"), ("b", "capture"))
    ])
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    assert case[2].buffers == {}
    for link in case[2].control_links:
        assert link["count"] == count
        assert link["src_slot"] == link["dst_slot"] == "values_storage"
        assert link["delivery"] == "before_call"
    check_four_entries(case, [[0, 2, expected[count - 1], 0], [1, 4, expected[count - 1], 8]])
