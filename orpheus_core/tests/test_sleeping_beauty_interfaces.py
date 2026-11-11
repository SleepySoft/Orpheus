"""共享组斜坡按真实调用推进；同值控制请求也保留重新建立目标的语义。"""
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
int orpheus_test_balance_source_algorithm(orpheus_test_balance_source_State* s,
    orpheus_test_balance_source_IO* io,const OrpheusProcessContext* ctx) {
    const float indices[]={10,0,255,128};
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++)
            io->out.samples[n*@CHANNELS@+c]=(float)((int)(((s->calls*@FRAMES@+n)*17+c*31)%103)-51)*.03125f;
        s->index=indices[s->calls];
        int rc=orpheus_test_balance_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''


def target_gains(index, mean, mute_bass):
    silent = f32(5.0118723e-7)
    cut = f32(10 ** {10: -1.5, 0: -1.0, 255: -2.0, 128: 0.0}[index])
    left, right = (cut, 1.0) if index > 128 else (1.0, cut)
    center = mono = cut
    if abs(index - 128) >= 127:
        if index > 128:
            left = 0.0
        else:
            right = 0.0
        center = 0.0
        if mute_bass:
            mono = 0.0
    elif mean:
        center = mono = f32(f32(left + right) * .5)
    return [max(value, silent) for value in (left, right, center, mono)]


@pytest.mark.parametrize("channels,frames,ms,rate,mean,mute_bass,frozen", [
    (6, 16, 1., 0., True, True, False), (64, 1, 0., 0., False, False, False),
    (3, 7, 30., 0., False, True, True), (4, 32, 30., 1000., False, True, False),
])
def test_grouped_balance_actual_calls(tmp_path, channels, frames, ms, rate, mean, mute_bass, frozen):
    channel_map = ([0, 1, 1, 2, 3, -1] if channels == 6 else
                   [-1 if c % 5 == 4 else c % 4 for c in range(channels)])
    current = [1.0] * 4
    expected = []
    for call, index in enumerate((10, 0, 255, 128)):
        targets = target_gains(index, mean, mute_bass)
        coefficients, remaining = [], []
        for r, target in enumerate(targets):
            current_db = f32(20 * f32(math.log10(max(current[r], f32(5.0118723e-7)))))
            target_db = f32(20 * f32(math.log10(target)))
            difference = abs(f32(target_db - current_db))
            if difference < f32(.01) or ms <= 0:
                current[r] = target
                coefficients.append(1.0)
                remaining.append(0)
                continue
            duration = f32(f32(difference / min(50000., max(5., rate))) * 1000) if rate > 0 else ms
            duration = min(3000., max(1., duration))
            block_ms = f32(f32(1000 * frames) / 48000)
            blocks = max(1, math.ceil(f32(duration / block_ms)))
            denominator = f32(f32(frames) * f32(blocks))
            coefficient = f32(math.exp(f32(f32(math.log(f32(target / max(current[r], f32(5.0118723e-7))))) /
                                               denominator)))
            coefficients.append(coefficient)
            remaining.append(blocks)
        for n in range(frames):
            if not frozen:
                current = [max(f32(g * c) if blocks else g, f32(5.0118723e-7))
                           for g, c, blocks in zip(current, coefficients, remaining)]
            for c, group in enumerate(channel_map):
                sample = (((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125
                expected.append(f32(sample * (current[group] if group >= 0 else 0.0)))
        if not frozen:
            current = [target if blocks == 1 else gain
                       for gain, target, blocks in zip(current, targets, remaining)]
    source = create_component(tmp_path, "balance_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
        "index": {**scalar("float", 128.0), "control_source": True},
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    balance = ROOT / "components" / "orpheus" / "builtin" / "sleeping_beauty" / "component.yaml"
    manifests = [source, balance, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.balance_source"),
        "balance": Node("balance", "orpheus.builtin.sleeping_beauty", params={
            "channels": channels, "ramp_ms": ms, "ramp_db_per_s": rate, "freeze": frozen,
            "center_mono_mode": "mean_lr" if mean else "attenuated", "mutes_bass": float(mute_bass),
            "apply_index_on_prepare": False, "chan_map": ",".join(map(str, channel_map)),
            "table_idx": "0,10,128,255", "table_db": "-20,-30,0,-40"}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("balance", "in")),
        Connection(PortRef("balance", "out"), PortRef("capture", "in")),
    ]), control_connections=[ControlConnection(PortRef("source", "index"), PortRef("balance", "gain_index"))])
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
