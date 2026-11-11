"""同帧M/S、高通递推和三旋钮逐样本平滑的四入口逐位预期。"""

import math

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_noise_slew_interfaces import CAPTURE
from test_spectral_reduce_interfaces import SOURCE, f32
from test_table_interfaces import check_four_entries, four_entry_harness


UPDATE = r'''
    for(int tick=0;tick<2;tick++) {
        if(tick==1) {
            const char* ids[]={"width","air","mono_mix"};
            const float values[]={.25f,.9f,.75f};
            for(unsigned i=0;i<3;i++) {
                OrpheusValue value={ORPHEUS_VALUE_FLOAT};value.value.f32=values[i];
#ifdef DYNAMIC
                if(runtime.set_parameter("enhancer",ids[i],value)) return 10;
#else
                if(orpheus_control_set_value("enhancer",ids[i],value)) return 10;
#endif
            }
        }
'''


def oracle(frames, width, air, mix, cutoff, update):
    smoothed = [f32(width), f32(air), f32(mix)]
    coefficient = f32(1 - f32(math.exp(f32(-1 / f32(f32(.005) * 48000)))))
    angle = f32(f32(f32(2 * f32(3.14159265)) * f32(cutoff)) / 48000)
    alpha = f32(f32(1 - f32(math.cos(angle))) / f32(1 + f32(math.sin(angle))))
    gain = f32(f32(1 + alpha) * .5)
    previous_side = previous_hp = 0.0
    expected = []
    for call in range(4):
        targets = [.25, f32(.9), .75] if update and call >= 2 else [width, air, mix]
        row = []
        for frame in range(frames):
            left = f32(((frame * 17) % 103 - 51) * .03125 + call * .125)
            right = f32(((frame * 17 + 31) % 103 - 51) * .03125 + call * .125)
            mid = f32(f32(left + right) * .5)
            side = f32(f32(left - right) * .5)
            smoothed = [f32(old + f32(coefficient * f32(f32(target) - old)))
                        for old, target in zip(smoothed, targets)]
            hp = f32(f32(gain * f32(side - previous_side)) + f32(alpha * previous_hp))
            previous_side, previous_hp = side, hp
            side_hp = f32(f32(side * f32(1 - smoothed[1])) + f32(hp * smoothed[1]))
            side_out = f32(f32(side_hp * smoothed[0]) * smoothed[2])
            row.extend((f32(mid + side_out), f32(mid - side_out)))
        expected.append(row)
    return "{" + ",".join("{" + ",".join(v.hex() + "f" for v in row) + "}" for row in expected) + "}"


@pytest.mark.parametrize("frames,width,air,mix,cutoff,update", [
    (1, 1, 0, 1, 6000, False), (17, 4, 0, 1, 500, True),
    (128, 2.5, .7, .25, 15000, True), (64, 0, 1, 0, 3000, True),
])
def test_spatial_enhancer_four_entries(tmp_path, frames, width, air, mix, cutoff, update):
    source = create_component(tmp_path, "spatial_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": 2, "drive_port_no": 1},
        "channels": scalar("int", 2), "calls": scalar("int", 0),
    }, SOURCE.replace("reduce_source", "spatial_source"), source=True)
    capture_source = CAPTURE.replace("slew_capture", "spatial_capture")
    capture_source = capture_source.replace("@FRAMES@", str(frames)).replace("@COUNT@", str(2 * frames))
    capture_source = capture_source.replace("@EXPECTED@", oracle(frames, width, air, mix, cutoff, update))
    capture = create_component(tmp_path, "spatial_capture", {
        "in": {"direction": "input", "type": "audio", "sample_format": "f32", "channels": 2, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, capture_source)
    manifest = ROOT / "components" / "orpheus" / "builtin" / "spatial_enhancer" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.spatial_source"),
        "enhancer": Node("enhancer", "orpheus.builtin.spatial_enhancer", params={
            "width": float(width), "air": float(air), "mono_mix": float(mix), "air_fc": float(cutoff),
        }),
        "capture": Node("capture", "orpheus.test.spatial_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("enhancer", "in")),
        Connection(PortRef("enhancer", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    if update:
        harness = harness.replace("for(int tick=0;tick<2;tick++) {", UPDATE)
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
