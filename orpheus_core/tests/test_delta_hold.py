"""真实 f32→i32 逐次交付，保持按批推进，不按 Task 帧数计时。"""

import struct

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_delta_source_algorithm(orpheus_test_delta_source_State* s,
    orpheus_test_delta_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<@REPEATS@;repeat++) {
        int32_t call=s->calls++;
        for(int32_t h=0;h<s->batches;h++) for(int32_t c=0;c<s->channels;c++) {
            int32_t phase=(call*s->batches+h+c)%@CYCLE@;
            @FILL@
        }
        int rc=orpheus_test_delta_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
int orpheus_test_delta_capture_algorithm(orpheus_test_delta_capture_State* s,
    orpheus_test_delta_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const int32_t expected[@CALLS@][@COUNT@]={@EXPECTED@};
    if(s->calls>=@CALLS@ || ctx->trigger_input || ctx->frame_count!=@STEP@ || ctx->valid_frames!=@STEP@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/@REPEATS@*@STEP@))
        return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned i=0;i<@COUNT@;i++)
        if(io->in.samples[i]!=expected[s->calls][i]) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def verify_delta_hold(tmp_path, *, channels=1, batches=1, threshold=1.5, hold_ticks=18, step=128,
                      reference=None, energy=False):
    repeats = max(2, (hold_ticks + 7 + batches - 1) // batches)
    cycle = hold_ticks + 5
    fill = ("io->out.samples[h*2*s->channels+c]=phase==0 ? 4.0f : 0.0f;"
            "io->out.samples[(h*2+1)*s->channels+c]=0.0f;" if energy else
            "io->out.samples[h*s->channels+c]=phase==0 ? 8.0f : phase==1 ? 8.0f+s->threshold : 0.0f;")
    source = create_component(tmp_path, "delta_source", {
        "out": {"direction": "output", "type": "bulk", "sample_format": "f32",
                "channels": "param:channels", "block_size": "2*param:batches" if energy else "param:batches",
                "drive_port_no": 1},
        "channels": parameter(channels, 64), "batches": parameter(batches, 64),
        "threshold": scalar("float", threshold), "calls": scalar("int", 0),
    }, SOURCE.replace("@REPEATS@", str(repeats)).replace("@CYCLE@", str(cycle)).replace("@FILL@", fill), source=True)
    previous, active, counters = [0.0] * channels, [0] * channels, [0] * channels
    expected, seen = [], set()
    for call in range(repeats * 2):
        samples, row = [], []
        for h in range(batches):
            for c in range(channels):
                phase = (call * batches + h + c) % cycle
                if energy:
                    current = 16.0 if phase == 0 else 0.0
                else:
                    current = 8.0 if phase == 0 else f32(8.0 + f32(threshold)) if phase == 1 else 0.0
                samples.append(current)
                if f32(current - previous[c]) > f32(threshold):
                    flag, counters[c] = 1, 0
                elif active[c] and counters[c] < hold_ticks:
                    flag = 1
                    counters[c] += 1
                else:
                    flag = 0
                previous[c], active[c] = current, flag
                row.append(flag)
        if reference is not None:
            assert struct.pack(f"<{len(row)}i", *row) == reference(struct.pack(f"<{len(samples)}f", *samples))
        expected.append(row)
        seen.update(row)
    assert seen == {0, 1}, "必须同时验收触发与保持到期。"
    code = CAPTURE
    for key, value in {
        "COUNT": channels * batches, "CALLS": repeats * 2, "STEP": step, "REPEATS": repeats,
        "EXPECTED": ",".join("{" + ",".join(map(str, row)) + "}" for row in expected),
    }.items():
        code = code.replace(f"@{key}@", str(value))
    capture = create_component(tmp_path, "delta_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "i32",
               "channels": channels, "block_size": batches, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code)
    manifest = ROOT / "components" / "orpheus" / "builtin" / "delta_hold" / "component.yaml"
    manifests = [source, manifest, capture]
    nodes = {
        "source": Node("source", "orpheus.test.delta_source"),
        "detector": Node("detector", "orpheus.builtin.delta_hold", params={
            "channels": channels, "batches": batches, "threshold": threshold, "hold_ticks": hold_ticks}),
        "capture": Node("capture", "orpheus.test.delta_capture"),
    }
    upstream = PortRef("source", "out")
    connections = [Connection(PortRef("detector", "out"), PortRef("capture", "in"))]
    if energy:
        manifests.append(ROOT / "components" / "orpheus" / "builtin" / "complex_power" / "component.yaml")
        nodes["power"] = Node("power", "orpheus.builtin.complex_power",
                              params={"channels": channels, "bins": 1, "batches": batches})
        connections.append(Connection(upstream, PortRef("power", "in")))
        upstream = PortRef("power", "energy")
    connections.append(Connection(upstream, PortRef("detector", "in")))
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=step, graph=Graph(nodes=nodes, connections=connections))
    harness = four_entry_harness().replace(",8)", f",{step})").replace("(8)", f"({step})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert all(config["frames"] == step for config in case[2].node_configs.values())
    check_four_entries(case, [[0, repeats, 0, 0], [1, repeats * 2, 0, step]])


@pytest.mark.parametrize("channels,batches,threshold,hold_ticks,step", [
    (1, 1, 1.5, 18, 128), (1, 3, 6.5, 4, 384), (3, 2, 1.5, 2, 32), (64, 64, 1.5, 0, 8),
])
def test_delta_hold_four_entries(tmp_path, channels, batches, threshold, hold_ticks, step):
    verify_delta_hold(tmp_path, channels=channels, batches=batches, threshold=threshold, hold_ticks=hold_ticks, step=step)


def test_delta_hold_after_complex_power(tmp_path):
    verify_delta_hold(tmp_path, channels=3, batches=2, hold_ticks=3, step=8, energy=True)
