"""同shape数据逐点相减，不把向量行数当作音频时间量子。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("channels,elements,step",[(2,129,128),(2,387,384),(3,5,32)])
def test_bulk_subtract_four_entries(tmp_path,channels,elements,step):
    registry=Registry([ROOT/"components"]);registry.scan()
    for name in ("array_pair","tensor_pair_capture"):
        registry.add_manifest(Path(__file__).parent/"fixtures"/name/"component.yaml")
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",params={"channels":channels,"elements":elements,"mode":0,"complex_mode":1}),
        "power":Node(id="power",component="orpheus.builtin.complex_power",params={"channels":channels,"bins":elements,"batches":1}),
        "subtract":Node(id="subtract",component="orpheus.builtin.bulk_subtract",params={"channels":channels,"elements":elements})}
    for name in ("inputs","result"):
        nodes[name]=Node(id=name,component="orpheus.test.tensor_pair_capture",
            params={"channels_a":channels,"channels_b":channels,"frames_a":elements,"frames_b":elements})
    pairs=[("source:complex_out0","power:in"),("source:out","subtract:a"),("power:power","subtract:b"),
           ("source:out","inputs:a"),("power:power","inputs:b"),("source:out","result:a"),("subtract:out","result:b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==step for c in plan.node_configs.values())
    count=channels*elements
    records=execute_array_graph(tmp_path,registry,plan,{"inputs":(count,count),"result":(count,count)})
    for (a,b),(original,actual) in zip(records["inputs"],records["result"]):
        assert original==a and a!=b
        expected=[x-y for x,y in zip(struct.unpack(f"<{count}f",a),struct.unpack(f"<{count}f",b))]
        assert actual==struct.pack(f"<{count}f",*expected) and any(x!=0 for x in expected)
