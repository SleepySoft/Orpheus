"""以独立位型和索引映射验证打包，不借助 FFT、线性重写或旧采集夹具。"""

import pytest

from frequency_interface_fixtures import PATTERNS, frequency_graph
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, create_component


def verify_halfcomplex_encode(tmp_path, specifications, batches):
    for name, params in specifications.items():
        channels, fft_size, bins = (params[key] for key in ("channels", "fft_size", "bins"))
        assert bins == fft_size // 2 + 1
        rows = []
        for call in range(4):
            values = [PATTERNS[(i + call) % 8] for i in range(channels * bins * 2 * batches)]
            expected = [0] * (channels * fft_size * batches)
            for batch in range(batches):
                for k in range(bins):
                    for channel in range(channels):
                        source = (batch * bins * 2 + k * 2) * channels + channel
                        expected[(batch * fft_size + k) * channels + channel] = values[source]
                        if 0 < k < fft_size // 2:
                            expected[(batch * fft_size + fft_size - k) * channels + channel] = values[source + channels]
            rows.append(expected)
        frequency_graph(tmp_path / name, "halfcomplex_encode", params,
                        (fft_size * batches, fft_size * batches), (rows, rows), typed=True, patterns=True)


@pytest.mark.parametrize("batches,channels,fft_size", [(1, 6, 256), (3, 8, 256), (64, 64, 2), (2, 3, 6)])
def test_halfcomplex_encode_four_entries(tmp_path, batches, channels, fft_size):
    verify_halfcomplex_encode(tmp_path, {"encode": {
        "channels": channels, "fft_size": fft_size, "batches": batches, "bins": fft_size // 2 + 1}}, batches)


def test_halfcomplex_frame_structure_is_part_of_the_connection_contract(tmp_path):
    capture = create_component(tmp_path, "layout_capture", {
        "in": {"direction": "input", "type": "bulk", "sample_format": "f32", "channels": 2,
               "block_size": 256, "layout": "halfcomplex", "frame_size": 128, "process": True},
    }, "")
    registry = Registry()
    registry.add_manifest(ROOT / "components" / "orpheus" / "builtin" / "halfcomplex_encode" / "component.yaml")
    registry.add_manifest(capture)
    project = Project(block_size=8, graph=Graph(nodes={
        "encode": Node("encode", "orpheus.builtin.halfcomplex_encode"),
        "capture": Node("capture", "orpheus.test.layout_capture"),
    }, connections=[Connection(PortRef("encode", "spectrum"), PortRef("capture", "in"))]))
    diagnostics = []
    GraphCompiler(registry).resolve_port_signatures(project, connection_diagnostics=diagnostics)
    assert diagnostics[0]["status"] == "mismatch"
    assert [(item["field"], item["source"], item["target"])
            for item in diagnostics[0]["differences"]] == [("frame_size", 256, 128)]
