"""共享池前缀重叠、逐位搬运及每次真实到达的四入口验收。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_channel_router_interfaces import SOURCE
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


@pytest.mark.parametrize("channels,capacity,enabled", [(1, 1, True), (4, 11, True), (64, 192000, True), (32, 7, False)])
def test_pool_each_arrival_keeps_overlapping_pool_and_original_bits(tmp_path, channels, capacity, enabled):
    delays = [(0 if c % 3 == 0 else capacity if c % 3 == 1 else 3) for c in range(channels)]
    patterns = [0x7FA12345, 0x80000000, 0x7FC76543, 1, 0x7F800000, 0xFF800000, 0x3F800000]
    pool, cursor, expected = [0] * capacity, 0, []
    for call in range(4):
        samples = [[patterns[(n + c + call % 2 + call // 2) % 7] for c in range(channels)] for n in range(8)]
        if not enabled:
            expected.extend(value for row in samples for value in row)
            continue
        columns, prefix = [], 0
        for c, delay in enumerate(delays):
            old = [pool[(cursor + prefix + n + 1) % capacity] for n in range(min(8, delay))]
            columns.append(old + [row[c] for row in samples[:8 - len(old)]])
            prefix += delay
        expected.extend(columns[c][n] for n in range(8) for c in range(channels))
        cursor = (cursor + 8) % capacity
        prefix = 0
        for c, delay in enumerate(delays):
            for n in range(max(0, 8 - delay), 8):
                pool[(cursor + prefix + delay - 7 + n) % capacity] = samples[n][c]
            prefix += delay
    source = create_component(tmp_path, "router_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "ticks": scalar("int", 0),
    }, SOURCE, source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(map(str, expected)))
        .replace("@FRAMES@", "8").replace("@COUNT@", str(channels * 8))
        .replace("s->last=io->in.samples[" + str(channels * 8) + "-1]", "s->last=0"))
    delay = ROOT / "components" / "orpheus" / "builtin" / "pool_delay" / "component.yaml"
    manifests = [source, delay, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.router_source"),
        "delay": Node("delay", "orpheus.builtin.pool_delay", params={
            "channels": channels, "pool_size_samples": capacity, "enabled": enabled,
            "delays_i32": delays + [0] * (64 - channels),
        }),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("delay", "in")),
        Connection(PortRef("delay", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace("    return 0;\n}", "#ifndef DYNAMIC\n"
                                          "    orpheus_generated_teardown();\n#endif\n    return 0;\n}")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, 8]])
