"""分段曲线、NaN 峰值首项和第 32 通道位图的四入口验证。"""

import math
import struct

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_channel_router_interfaces import SOURCE
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("channels,param_set,disabled", [(1, 0, False), (32, 1, False), (32, 1, True)])
def test_piecewise_each_delivery_keeps_curve_bits_and_mask(tmp_path, channels, param_set, disabled):
    patterns = [0x7FA12345, 0x80000000, 0x7FC76543, 1, 0x7F800000, 0xFF800000, 0x3F800000]
    xmin, xmax, p2 = (f32(.65), f32(1.35), f32(.714285731)) if param_set else (.25, 1.5, .25)
    expected, masks = [], []
    for call in range(4):
        samples = [[patterns[(n + c + call % 2 + call // 2) % 7] for c in range(channels)] for n in range(8)]
        mask = 0
        for c in range(channels):
            magnitudes = [abs(struct.unpack("<f", struct.pack("<I", row[c]))[0]) for row in samples]
            peak = magnitudes[0]
            for value in magnitudes[1:]:
                if value > peak:
                    peak = value
            if not disabled and peak > xmin:
                mask |= 1 << c
        masks.append(mask if mask < 2**31 else mask - 2**32)
        for row in samples:
            for value in row:
                magnitude = abs(struct.unpack("<f", struct.pack("<I", value))[0])
                limited = xmax if math.isnan(magnitude) else min(magnitude, xmax)
                excess = max(f32(limited - xmin), 0)
                result = f32(limited - f32(f32(excess * excess) * p2))
                expected.append(value if disabled else (bits(result) & 0x7FFFFFFF) | (value & 0x80000000))
    source = create_component(tmp_path, "router_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "ticks": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(map(str, expected)))
        .replace("@FRAMES@", "8").replace("@COUNT@", str(channels * 8))
        .replace(f"s->last=io->in.samples[{channels * 8}-1]", "s->last=0"))
    clipper = ROOT / "components" / "orpheus" / "builtin" / "piecewise_soft_clipper" / "component.yaml"
    manifests = [source, clipper, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.router_source"),
        "clipper": Node("clipper", "orpheus.builtin.piecewise_soft_clipper", params={
            "channels": channels, "param_set": param_set, "disabled": disabled,
            "xmin_low": .25, "xmax_low": 1.5, "p2_low": .25,
        }),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("clipper", "in")),
        Connection(PortRef("clipper", "out"), PortRef("capture", "in")),
    ]))
    probe = r'''
        OrpheusValue mask={ORPHEUS_VALUE_INT};
        const int expected_masks[]={@MASKS@};
#ifdef DYNAMIC
        if(runtime.get_parameter("clipper","active_mask",&mask)) return 8;
#else
        if(orpheus_control_get_value("clipper","active_mask",&mask)) return 8;
#endif
        if(mask.type!=ORPHEUS_VALUE_INT || mask.value.i32!=expected_masks[tick]) return 9;
'''
    harness = four_entry_harness().replace("        printf(",
        probe.replace("@MASKS@", f"{masks[1]},{masks[3]}") + "        printf(")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, 8]])
