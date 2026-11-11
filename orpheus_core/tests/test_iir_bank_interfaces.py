"""级联DF2T逐项f32预期、异构级数与每次真实交付的四入口验证。"""
import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("channels,frames,stages,per_channel", [
    (1, 1, 1, False), (3, 7, 3, True), (64, 3, 16, False), (64, 17, 16, True),
])
def test_iir_bank_every_arrival_advances_actual_cascade(tmp_path, channels, frames, stages, per_channel):
    counts = [stages if channels == 1 else c % (stages + 1) for c in range(channels)]
    rows = []
    for channel in range(channels if per_channel else 1):
        rows.append([
            [f32((channel % 3 + 1) / 5), .125, -.0625, -.25, .03125]
            if not per_channel or stage < counts[channel] else [float("nan")] * 5
            for stage in range(stages)
        ])
    history = [[[0.0, 0.0] for _ in range(stages)] for _ in range(channels)]
    expected = []
    for n in range(4 * frames):
        for channel in range(channels):
            x = ((n * 17 + channel * 31) % 103 - 51) * .03125
            for stage in range(counts[channel]):
                b0, b1, b2, a1, a2 = rows[channel if per_channel else 0][stage]
                z1, z2 = history[channel][stage]
                y = f32(f32(b0 * x) + z1)
                history[channel][stage] = [
                    f32(f32(f32(b1 * x) + z2) - f32(a1 * y)),
                    f32(f32(b2 * x) - f32(a2 * y)),
                ]
                x = y
            expected.append(x)
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    bank = ROOT / "components" / "orpheus" / "builtin" / "iir_bank" / "component.yaml"
    manifests = [source, bank, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"channels": channels, "num_stages": stages,
              "coefs_mode": "per_channel" if per_channel else "shared",
              "stages_per_channel": ",".join(map(str, counts))}
    params["initial_coefs_per_channel" if per_channel and channels == 64 else "coefs"] = ",".join(
        str(value) for row in rows for stage in row for value in stage)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "bank": Node("bank", "orpheus.builtin.iir_bank", params=params),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("bank", "in")),
        Connection(PortRef("bank", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    key = "coefs_per_channel" if per_channel else "coefs"
    slot = next(slot for slot in case[2].id_map if slot["node"] == "bank" and slot["key"] == key)
    assert slot["count"] == (channels if per_channel else 1) * 80 and slot["form"] == "BULK"
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
