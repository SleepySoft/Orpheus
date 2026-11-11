"""三种FIR系数布局、反向f32卷积和独立交付次数的四入口验证。"""
import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("mode,channels,frames,taps,filters", [
    ("shared", 3, 7, 3, 64), ("per_channel", 3, 7, 7, 64),
    ("banked", 3, 7, 17, 3), ("shared", 64, 1, 4096, 64),
    ("per_channel", 64, 1, 512, 64), ("banked", 64, 1, 512, 64),
])
def test_fir_bank_every_arrival_matches_reverse_convolution(tmp_path, mode, channels, frames, taps, filters):
    sets = filters if mode == "banked" else channels if mode == "per_channel" else 1
    rows = ([[1.0] + [0.0] * (taps - 1) for _ in range(sets)] if taps > 32 else [
        [f32((c % 3 + 1) / 5)] + [f32((k % 5 - 2) / 103) for k in range(taps - 1)]
        for c in range(sets)])
    if mode == "shared" and taps == 3:
        rows[0] = [f32(1e20), f32(-1e20), 1.0]
    lengths = [0 if c % 3 == 0 else 1 if c % 3 == 1 else taps for c in range(channels)]
    indices = [c % filters for c in range(channels)]
    expected = []
    for n in range(4 * frames):
        for channel in range(channels):
            coefficients = rows[indices[channel] if mode == "banked" else channel if mode == "per_channel" else 0]
            total = 0.0
            for k in range(lengths[channel] - 1, -1, -1):
                x = 0.0 if n < k else (((n - k) * 17 + channel * 31) % 103 - 51) * .03125
                total = f32(total + f32(coefficients[k] * x))
            expected.append(total)
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    bank = ROOT / "components" / "orpheus" / "builtin" / "fir_bank" / "component.yaml"
    manifests = [source, bank, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"channels": channels, "num_taps": taps, "num_filters": filters,
              "coefs_mode": mode, "taps_per_channel": ",".join(map(str, lengths))}
    if taps <= 32:
        params["coefs"] = ",".join(str(v) for row in rows for v in row)
    if mode == "banked":
        params["filter_indices"] = ",".join(map(str, indices))
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "bank": Node("bank", "orpheus.builtin.fir_bank", params=params),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("bank", "in")),
        Connection(PortRef("bank", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    selected = "coefs_banked" if mode == "banked" else "coefs_per_channel" if mode == "per_channel" else "coefs"
    slots = {p["key"]: p for p in case[2].id_map if p["node"] == "bank" and p["form"] == "BULK"}
    assert set(slots) == {selected, *({"filter_indices_i32"} if mode == "banked" else set())}
    assert slots[selected]["count"] == sets * taps
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
