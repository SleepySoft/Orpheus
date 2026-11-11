"""直接型多项式IIR逐项f32递推与每次实际到达的四入口验证。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("channels,frames,order,per_channel", [
    (1, 1, 1, False), (3, 7, 4, True), (64, 17, 16, False), (64, 3, 16, True),
])
def test_direct_iir_each_arrival_matches_f32_state_recursion(tmp_path, channels, frames, order, per_channel):
    rows = []
    for channel in range(channels if per_channel else 1):
        rows.append([f32((channel % 7 + 1) / 17)] +
                    [f32((k % 5 - 2) / 103) for k in range(order)] +
                    [-.125] + [0.0] * (order - 1))
    history = [[0.0] * order for _ in range(channels)]
    expected = []
    for n in range(4 * frames):
        for channel in range(channels):
            x = ((n * 17 + channel * 31) % 103 - 51) * .03125
            c, z = rows[channel if per_channel else 0], history[channel]
            y = f32(f32(c[0] * x) + z[0])
            for k in range(order - 1):
                z[k] = f32(f32(z[k + 1] + f32(c[k + 1] * x)) - f32(c[order + k + 1] * y))
            z[-1] = f32(f32(c[order] * x) - f32(c[2 * order] * y))
            expected.append(y)
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    iir = ROOT / "components" / "orpheus" / "builtin" / "iir" / "component.yaml"
    manifests = [source, iir, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"channels": channels, "order": order, "coefs_mode": "per_channel" if per_channel else "shared"}
    params["initial_coefs_per_channel" if per_channel and channels == 64 else "coefs"] = ",".join(
        str(value) for row in rows for value in row)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "iir": Node("iir", "orpheus.builtin.iir", params=params),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("iir", "in")),
        Connection(PortRef("iir", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    for key, count in (("coefs", 33), ("coefs_per_channel", channels * 33)):
        slot = next(slot for slot in case[2].id_map if slot["node"] == "iir" and slot["key"] == key)
        assert slot["count"] == count and slot["form"] == "BULK"
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
