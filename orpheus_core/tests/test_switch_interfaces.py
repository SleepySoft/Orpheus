"""音频开关阈值及按交错样本递推斜坡的真实四入口逐位预期。"""

import math

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_noise_slew_interfaces import CAPTURE
from test_spectral_reduce_interfaces import SOURCE, f32
from test_table_interfaces import check_four_entries, four_entry_harness


UPDATE = r'''
    for(int tick=0;tick<2;tick++) {
        if(tick==1) {
            OrpheusValue value={ORPHEUS_VALUE_FLOAT};value.value.f32=@ENABLE@;
#ifdef DYNAMIC
            if(runtime.set_parameter("gate","enable",value)) return 10;
#else
            if(orpheus_control_set_value("gate","enable",value)) return 10;
#endif
        }
'''


def oracle(channels, frames, initial, ramp, updated):
    coefficient = 1.0 if ramp == 0 else f32(
        1 - f32(math.exp(f32(-1 / f32(f32(f32(ramp) / 1000) * 48000)))))
    gain = 1.0 if f32(initial) >= .5 else 0.0
    expected = []
    for call in range(4):
        target = 1.0 if f32(updated if call >= 2 else initial) >= .5 else 0.0
        row = []
        for frame in range(frames):
            for channel in range(channels):
                sample = f32(((frame * 17 + channel * 31) % 103 - 51) * .03125 + call * .125)
                gain = f32(gain + f32(coefficient * f32(target - gain)))
                row.append(f32(sample * gain))
        expected.append(row)
    return "{" + ",".join("{" + ",".join(v.hex() + "f" for v in row) + "}" for row in expected) + "}"


@pytest.mark.parametrize("channels,frames,initial,ramp,updated", [
    (1, 1, 1.0, 20.0, 0.0), (3, 17, .49, 0.0, .5),
    (32, 128, .5, 1000.0, .499), (2, 7, 0.0, 1.0, 1.0),
])
def test_switch_four_entries(tmp_path, channels, frames, initial, ramp, updated):
    source = create_component(tmp_path, "switch_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": "param:channels", "drive_port_no": 1},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
    }, SOURCE.replace("reduce_source", "switch_source"), source=True)
    capture_source = CAPTURE.replace("slew_capture", "switch_capture")
    capture_source = capture_source.replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames))
    capture_source = capture_source.replace("@EXPECTED@", oracle(channels, frames, initial, ramp, updated))
    capture = create_component(tmp_path, "switch_capture", {
        "in": {"direction": "input", "type": "audio", "sample_format": "f32",
               "channels": "param:channels", "process": True},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, capture_source)
    manifest = ROOT / "components" / "orpheus" / "builtin" / "switch" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.switch_source"),
        "gate": Node("gate", "orpheus.builtin.switch", params={
            "channels": channels, "enable": initial, "ramp_ms": ramp,
        }),
        "capture": Node("capture", "orpheus.test.switch_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("gate", "in")),
        Connection(PortRef("gate", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("for(int tick=0;tick<2;tick++) {",
                              UPDATE.replace("@ENABLE@", f32(updated).hex() + "f"))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
