"""数据有序归约输出一行，独立于图块长且不广播。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("channels,elements,step",[(4,65,128),(2,387,384),(128,1,32)])
def test_bulk_sum_four_entries(tmp_path,channels,elements,step):
    registry=Registry([ROOT/"components"]);registry.scan()
    for name in ("array_pair","tensor_pair_capture"):
        registry.add_manifest(Path(__file__).parent/"fixtures"/name/"component.yaml")
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",params={"channels":channels,"elements":elements,"mode":0}),
        "sum":Node(id="sum",component="orpheus.builtin.bulk_sum",params={"channels":channels,"elements":elements}),
        "capture":Node(id="capture",component="orpheus.test.tensor_pair_capture",params={
            "channels_a":channels,"frames_a":elements,"channels_b":channels,"frames_b":1})}
    pairs=[("source:out","sum:in"),("source:out","capture:a"),("sum:out","capture:b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==step for c in plan.node_configs.values())
    records=execute_array_graph(tmp_path,registry,plan,{"capture":(channels*elements,channels)})["capture"]
    for source,actual in records:
        values=struct.unpack(f"<{channels*elements}f",source);expected=[]
        for c in range(channels):
            acc=values[c]
            for i in range(1,elements):acc=struct.unpack("<f",struct.pack("<f",acc+values[i*channels+c]))[0]
            expected.append(acc)
        assert actual==struct.pack(f"<{channels}f",*expected) and len(actual)==channels*4
