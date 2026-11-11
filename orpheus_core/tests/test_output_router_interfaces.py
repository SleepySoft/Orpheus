"""静态路由保留单位阵尾部及零增益跳项，不与普通矩阵乘法混同。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_matrix_mul_interfaces import CAPTURE
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


@pytest.mark.parametrize("rows,cols,text", [
    (1, 1, None), (3, 2, ".25"), (2, 3, ""),
    (32, 32, ",".join(str(f32(((i * 17) % 97 - 48) / 103)) for i in range(1024))),
])
def test_output_router_each_arrival_keeps_identity_tail_and_full_matrix(tmp_path, rows, cols, text):
    matrix = [float(r == c) for r in range(rows) for c in range(cols)]
    if text:
        values = [f32(float(v)) for v in text.split(",")]
        matrix[:len(values)] = values
    expected = []
    for n in range(32):
        frame = [((n * 17 + c * 31) % 103 - 51) * .03125 for c in range(cols)]
        for r in range(rows):
            total = 0.0
            for c in range(cols):
                gain = matrix[r * cols + c]
                if gain:
                    total = f32(total + f32(gain * frame[c]))
            expected.append(total)
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=cols, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(cols)).replace("@FRAMES@", "8"), source=True)
    capture = create_component(tmp_path, "matrix_capture", {
        "in": audio("input", channels=rows, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@ROWS@", str(rows)).replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected)))
    router = ROOT / "components" / "orpheus" / "builtin" / "output_router" / "component.yaml"
    manifests = [source, router, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"channels_in": cols, "channels_out": rows}
    if text is not None:
        params["matrix"] = text
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "router": Node("router", "orpheus.builtin.output_router", params=params),
        "capture": Node("capture", "orpheus.test.matrix_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("router", "in")),
        Connection(PortRef("router", "out"), PortRef("capture", "in")),
    ]))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    check_four_entries(case, [[0, 2, expected[16 * rows - 1], 0], [1, 4, expected[-1], 8]])
