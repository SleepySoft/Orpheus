"""数据尺寸与处理量子不同，纯计算图仍按Task节拍同轮执行。"""
from pathlib import Path
import struct

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).parent / "fixtures/array_pair"


def verify_complementary_coefficients(tmp_path, *, channels=2, elements=129, step=128, total=1.0, reference=None):
    registry = Registry([ROOT / "components"]); registry.scan()
    registry.add_manifest(FIXTURE / "component.yaml")
    nodes = {
        "source": Node(id="source", component="orpheus.test.array_pair",
                       params={"channels": channels, "elements": elements, "mode": 0}),
        "math": Node(id="math", component="orpheus.builtin.complementary_coefficients",
                     params={"channels": channels, "elements": elements, "total": total}),
        "capture": Node(id="capture", component="orpheus.test.array_pair",
                        params={"channels": channels, "elements": elements, "mode": 1}),
    }
    edges = [Connection(PortRef.parse(a), PortRef.parse(b)) for a, b in
             (("source:out", "math:in"), ("source:out", "capture:a"), ("math:out", "capture:b"))]
    project = Project(metadata={"name": "互补数组计算"}, tasks={
        "default": Task(id="default", sample_rate=48000, block_size=step)}, graph=Graph(nodes=nodes, connections=edges))
    plan = GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"] == step and set(plan.schedule["periods"].values()) == {1}
    for config in plan.node_configs.values():
        assert config["frames"] == config.get("frame_step", config["frames"]) == step
    assert plan.execution_order.index("source")<plan.execution_order.index("math")<plan.execution_order.index("capture")
    assert all(b["type"]=="bulk" and b["frame_count"]==elements for b in plan.buffers.values())
    count = channels*elements
    records = execute_array_graph(tmp_path,registry,plan,{"capture": (count,count*2)})
    for block, (packed, decoded) in enumerate(records["capture"]):
        expected_input = struct.pack(f"<{count}f", *[((i*17+block*13)%129-64)*.03125 for i in range(count)])
        assert packed == expected_input
        if reference is not None:
            expected = reference(packed)
        else:
            values = struct.unpack(f"<{count}f", packed)
            expected = b"".join(packed[row*channels*4:(row+1)*channels*4]+struct.pack(
                f"<{channels}f", *(total-x for x in values[row*channels:(row+1)*channels])) for row in range(elements))
        assert len(expected)==count*8 and decoded==expected, block


@pytest.mark.parametrize("channels,elements,step,total", [(2,129,128,1.0),(2,387,384,1.0),(1,7,32,-0.5)])
def test_complementary_coefficients_four_entries(tmp_path, channels, elements, step, total):
    verify_complementary_coefficients(tmp_path, channels=channels, elements=elements, step=step, total=total)
