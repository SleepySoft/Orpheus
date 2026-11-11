"""宿主缓冲组件每次真实上游到达均复制并交付，不按Task去重。"""

import struct

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_delay_line_interfaces import CAPTURE, SOURCE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_host_audio import add_host_clock, execute_host_graph
from test_table_interfaces import bits, check_four_entries, four_entry_harness


@pytest.mark.parametrize("channels,frames", [(3, 7), (64, 128)])
def test_host_buffers_copy_each_arrival_bitwise(tmp_path, channels, frames):
    count = channels * frames
    expected = [(((call * frames + n) * 17 + c * 31) % 103 - 51) * .03125
                for call in range(4) for n in range(frames) for c in range(channels)]
    for call in range(4):
        expected[call * count] = -0.0
    source_code = SOURCE.replace(
        "        int rc=", "        io->out.samples[0]=-0.0f;\n        int rc=")
    source = create_component(tmp_path, "delay_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, source_code.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "delay_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(value)) for value in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(count)))
    host = ROOT / "components" / "orpheus" / "builtin" / "host_audio" / "component.yaml"
    manifests = [source, host, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.delay_source"),
        "relay": Node("relay", "orpheus.builtin.host_audio", params={"channels": channels}),
        "host_out": Node("host_out", "orpheus.builtin.host_audio", params={"channels": channels, "mode": 1}),
        "capture": Node("capture", "orpheus.test.delay_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("relay", "in")),
        Connection(PortRef("relay", "out"), PortRef("host_out", "in")),
        Connection(PortRef("host_out", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    for node in ("relay", "host_out"):
        slot = next(item for item in case[2].id_map if item["node"] == node and item["key"] == "samples")
        assert slot["count"] == count and slot["type"] == "float"
    check_four_entries(case, [
        [0, 2, expected[2 * count - 1], 0], [1, 4, expected[-1], frames],
    ])


@pytest.mark.parametrize("component,channels,frames", [("host_audio", 3, 32), ("buffer_source", 64, 128)])
def test_host_entries_consume_submitted_blocks_four_entries(tmp_path, component, channels, frames):
    registry = Registry([ROOT / "components"])
    registry.scan()
    nodes = {
        "host_in": Node("host_in", f"orpheus.builtin.{component}", params={"channels": channels}),
        "host_out": Node("host_out", "orpheus.builtin.host_audio", params={"channels": channels, "mode": 1}),
    }
    connections = [Connection(PortRef("host_in", "out"), PortRef("host_out", "in"))]
    if component == "host_audio":
        nodes["clock"] = Node("clock", add_host_clock(tmp_path, registry))
        connections.insert(0, Connection(PortRef("clock", "out"), PortRef("host_in", "tick")))
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes=nodes, connections=connections))
    plan = GraphCompiler(registry).compile(project)
    if component == "host_audio":
        assert plan.node_configs["clock"]["output_drive_ports"] == {"out": 1}
    else:
        assert plan.tasks[0]["source"] == "host_in"
        assert plan.node_configs["host_in"]["input_ports"] == []
    assert plan.node_configs["host_in"]["host_ports"][0]["connected"] is False
    result = execute_host_graph(tmp_path, registry, plan, {"host_in": channels}, {"host_out": channels})
    expected = [(((block * 17 + frame * 3 + channel * 13) % 257) - 128) * .0009765625
                for block in range(24) for frame in range(frames) for channel in range(channels)]
    assert result == struct.pack("<" + str(len(expected)) + "f", *expected)
