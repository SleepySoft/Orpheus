"""逐次双出口梯形递推，按批推进独立历史并逐位验证舍入。"""

import struct

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_dual_source_algorithm(orpheus_test_dual_source_State* s,
    orpheus_test_dual_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        int32_t call=s->calls++;
        for(int32_t i=0;i<s->channels*s->bins*s->batches;i++)
            io->out.samples[i]=((i*17+call*13)%129-64)*.03125f;
        int rc=orpheus_test_dual_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_dual_capture_algorithm(orpheus_test_dual_capture_State* s,
    orpheus_test_dual_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[4][2][@COUNT@]={@EXPECTED@};
    unsigned port=ctx->trigger_input;
    if(s->calls>=8 || port!=(unsigned)(s->calls%2) || ctx->frame_count!=@STEP@ || ctx->valid_frames!=@STEP@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/4*@STEP@))
        return ORPHEUS_ERR_INVALID_ARG;
    const float* samples=port ? io->slow.samples : io->fast.samples;
    for(unsigned i=0;i<@COUNT@;i++) {
        uint32_t actual;
        memcpy(&actual,samples+i,4);
        if(actual!=expected[s->calls/2][port][i]) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def verify_dual_trapezoid_smooth(tmp_path, *, channels=1, bins=129, batches=1, step=128,
                                fast_alpha=0.5, slow_alpha=0.95, reference=None, initial=None):
    source = create_component(tmp_path, "dual_source", {
        "out": {"direction": "output", "type": "bulk", "sample_format": "f32",
                "channels": "param:channels", "block_size": "param:bins*param:batches", "drive_port_no": 1},
        "channels": parameter(channels, 64), "bins": parameter(bins, 65536),
        "batches": parameter(batches, 64), "calls": scalar("int", 0),
    }, SOURCE, source=True)
    seed = f32(0.0 if initial is None else initial)
    points, count = channels * bins, channels * bins * batches
    previous, histories = [seed] * points, [[seed] * points for _ in range(2)]
    alphas = [f32(fast_alpha), f32(slow_alpha)]
    factors = [f32(f32(1.0 - alpha) * .5) for alpha in alphas]
    expected = []
    for call in range(4):
        samples = [((i * 17 + call * 13) % 129 - 64) * .03125 for i in range(count)]
        row = [[], []]
        for h in range(batches):
            for i in range(points):
                current = samples[h * points + i]
                pair = f32(current + previous[i])
                for p in range(2):
                    value = f32(f32(pair * factors[p]) + f32(histories[p][i] * alphas[p]))
                    histories[p][i] = value
                    row[p].append(value)
                previous[i] = current
        if reference is not None:
            assert tuple(struct.pack(f"<{count}f", *values) for values in row) == reference(
                struct.pack(f"<{count}f", *samples))
        expected.append([[bits(value) for value in values] for values in row])
    port = {"direction": "input", "type": "bulk", "sample_format": "f32",
            "channels": channels, "block_size": bins * batches, "process": True}
    capture = create_component(tmp_path, "dual_capture", {
        "fast": port, "slow": dict(port),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@COUNT@", str(count)).replace("@STEP@", str(step)).replace(
        "@EXPECTED@", ",".join("{" + ",".join("{" + ",".join(map(str, values)) + "}"
                                            for values in row) + "}" for row in expected)))
    manifest = ROOT / "components" / "orpheus" / "builtin" / "dual_trapezoid_smooth" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=step, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.dual_source"),
        "smooth": Node("smooth", "orpheus.builtin.dual_trapezoid_smooth", params={
            "channels": channels, "bins": bins, "batches": batches,
            "fast_alpha": fast_alpha, "slow_alpha": slow_alpha, "initial": seed}),
        "capture": Node("capture", "orpheus.test.dual_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("smooth", "in")),
        Connection(PortRef("smooth", "fast"), PortRef("capture", "fast")),
        Connection(PortRef("smooth", "slow"), PortRef("capture", "slow")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{step})").replace("(8)", f"({step})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert all(config["frames"] == step for config in case[2].node_configs.values())
    check_four_entries(case, [[0, 4, 0, 0], [1, 8, 0, step]])


@pytest.mark.parametrize("channels,bins,batches,step", [
    (1, 129, 1, 128), (1, 129, 3, 384), (3, 7, 2, 32), (64, 1, 64, 8),
])
def test_dual_trapezoid_smooth_four_entries(tmp_path, channels, bins, batches, step):
    verify_dual_trapezoid_smooth(tmp_path, channels=channels, bins=bins, batches=batches, step=step)


@pytest.mark.parametrize("fast_alpha,slow_alpha", [(0.2, 0.8), (0.0, 1.0)])
def test_dual_trapezoid_nonzero_initial_four_entries(tmp_path, fast_alpha, slow_alpha):
    verify_dual_trapezoid_smooth(tmp_path, channels=3, bins=7, batches=3, step=64,
                                fast_alpha=fast_alpha, slow_alpha=slow_alpha, initial=3.25)
