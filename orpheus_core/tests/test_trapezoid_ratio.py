"""两个处理入口各自推进四组历史，不会合、不补调、不去重。"""

import struct

import pytest

from array_interface_fixtures import register_array_interfaces
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_math_interfaces import BULK_CAPTURE, divide
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


def verify_trapezoid_ratio(tmp_path, *, channels=1, bins=129, batches=1, alpha=0.9,
                           epsilon=2.22044605e-16, step=128, division_mode=0, reference=None):
    count, points = channels * bins * batches, channels * bins
    alpha, epsilon = f32(alpha), f32(epsilon)
    factor = f32(f32(1.0 - alpha) * .5)
    previous, histories = [[0.0] * points for _ in range(2)], [[0.0] * points for _ in range(2)]
    expected = []
    for tick in range(2):
        for second in range(2):
            samples = [
                [((i * 17 + tick * 13) % 129 - 64) * .03125 for i in range(count)],
                [(((i * 11 + (tick if second else tick - 1) * 7) % 97 - 48) * .0625
                  if tick or second else 0.0) for i in range(count)],
            ]
            row = []
            for h in range(batches):
                for i in range(points):
                    smoothed = []
                    for p in range(2):
                        current = samples[p][h * points + i]
                        value = f32(f32(f32(current + previous[p][i]) * factor)
                                    + f32(histories[p][i] * alpha))
                        previous[p][i], histories[p][i] = current, value
                        smoothed.append(value)
                    denominator = f32(smoothed[1] + epsilon)
                    row.append(f32(divide(1.0, denominator) * smoothed[0]) if division_mode
                               else divide(smoothed[0], denominator))
            if reference is not None:
                assert struct.pack(f"<{count}f", *row) == reference(
                    *(struct.pack(f"<{count}f", *values) for values in samples))
            expected.append([bits(value) for value in row])
    registry = Registry()
    register_array_interfaces(tmp_path, registry, paired_source=True)
    code = BULK_CAPTURE.replace("bulk_math_capture", "ratio_capture").replace(
        "!=8", f"!={step}").replace("/2*8", f"/2*{step}")
    capture = create_component(tmp_path, "ratio_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "f32",
               "channels": channels, "block_size": bins * batches, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code.replace("@COUNT@", str(count)).replace(
        "@EXPECTED@", ",".join("{" + ",".join(map(str, row)) + "}" for row in expected)))
    manifest = ROOT / "components" / "orpheus" / "builtin" / "trapezoid_ratio" / "component.yaml"
    registry.add_manifest(manifest)
    registry.add_manifest(capture)
    manifests = [tmp_path / "array_source" / "component.yaml", manifest, capture]
    project = Project(flow_mode="task_sequence", block_size=step, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.array_source", params={"channels": channels, "elements": bins * batches}),
        "ratio": Node("ratio", "orpheus.builtin.trapezoid_ratio", params={
            "channels": channels, "bins": bins, "batches": batches, "alpha": alpha,
            "epsilon": epsilon, "division_mode": division_mode}),
        "capture": Node("capture", "orpheus.test.ratio_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("ratio", "numerator")),
        Connection(PortRef("source", "other"), PortRef("ratio", "denominator")),
        Connection(PortRef("ratio", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{step})").replace("(8)", f"({step})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert all(config["frames"] == step for config in case[2].node_configs.values())
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, step]])


@pytest.mark.parametrize("channels,bins,batches,alpha,step", [
    (1, 129, 1, .9, 128), (1, 129, 3, .8, 384), (3, 5, 2, .5, 32), (64, 1, 64, .5, 8),
])
@pytest.mark.parametrize("division_mode", [0, 1])
def test_trapezoid_ratio_four_entries(tmp_path, channels, bins, batches, alpha, step, division_mode):
    verify_trapezoid_ratio(tmp_path, channels=channels, bins=bins, batches=batches, alpha=alpha,
                           step=step, division_mode=division_mode)


@pytest.mark.parametrize("alpha", [0.0, 1.0])
@pytest.mark.parametrize("division_mode", [0, 1])
def test_trapezoid_ratio_alpha_endpoints_and_zero_denominator(tmp_path, alpha, division_mode):
    verify_trapezoid_ratio(tmp_path, channels=2, bins=3, batches=2, alpha=alpha, epsilon=0.0,
                           step=8, division_mode=division_mode)
