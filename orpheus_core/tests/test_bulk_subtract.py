"""同shape数据逐点相减，不把向量行数当作音频时间量子。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph
from array_interface_fixtures import register_array_interfaces

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("channels,elements,step",[(2,129,128),(2,387,384),(3,5,32)])
def test_bulk_subtract_four_entries(tmp_path,channels,elements,step):
    registry=Registry([ROOT/"components"]);registry.scan()
    register_array_interfaces(tmp_path,registry,paired_source=True)
    nodes={
        "source":Node(id="source",component="orpheus.test.array_source",params={"channels":channels,"elements":elements}),
        "subtract":Node(id="subtract",component="orpheus.builtin.bulk_subtract",params={"channels":channels,"elements":elements})}
    for name in ("inputs","result"):
        nodes[name]=Node(id=name,component="orpheus.test.array_capture",
            params={"channels_a":channels,"channels_b":channels,"frames_a":elements,"frames_b":elements})
    pairs=[("source:out","subtract:a"),("source:other","subtract:b"),
           ("source:out","inputs:a"),("source:other","inputs:b"),("source:out","result:a"),("subtract:out","result:b")]
    project=Project(flow_mode="task_sequence",sample_rate=48000,block_size=step,
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==step for c in plan.node_configs.values())
    count=channels*elements
    records=execute_array_graph(tmp_path,registry,plan,{"inputs":(count,count),"result":(count,count)},
        calls_per_block={"inputs":2,"result":3})
    for block,((a,b),(original,actual)) in enumerate(zip(records["inputs"],records["result"])):
        assert a==struct.pack(f"<{count}f",*[((i*17+block*13)%129-64)*0.03125 for i in range(count)])
        assert b==struct.pack(f"<{count}f",*[((i*11+block*7)%97-48)*0.0625 for i in range(count)])
        assert original==a and a!=b
        expected=[x-y for x,y in zip(struct.unpack(f"<{count}f",a),struct.unpack(f"<{count}f",b))]
        assert actual==struct.pack(f"<{count}f",*expected) and any(x!=0 for x in expected)
