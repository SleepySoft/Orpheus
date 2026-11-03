"""两份独立输入与四组历史；逐批平滑比值的四入口逐位验收。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).parent/"fixtures"


def verify_trapezoid_ratio(tmp_path,*,channels=1,bins=129,batches=1,alpha=0.9,epsilon=2.22044605e-16,step=128,reference=None):
    registry=Registry([ROOT/"components"]);registry.scan()
    for fixture in ("array_pair","tensor_pair_capture"):registry.add_manifest(FIXTURES/fixture/"component.yaml")
    elements=bins*batches;count=channels*elements
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",params={"channels":channels,"elements":elements,"mode":0,"complex_mode":1}),
        "power":Node(id="power",component="orpheus.builtin.complex_power",params={"channels":channels,"bins":bins,"batches":batches}),
        "ratio":Node(id="ratio",component="orpheus.builtin.trapezoid_ratio",
                     params={"channels":channels,"bins":bins,"batches":batches,"alpha":alpha,"epsilon":epsilon})}
    for name in ("inputs","result"):
        nodes[name]=Node(id=name,component="orpheus.test.tensor_pair_capture",
            params={"channels_a":channels,"channels_b":channels,"frames_a":elements,"frames_b":elements})
    pairs=[("source:complex_out0","power:in"),("source:out","ratio:numerator"),("power:power","ratio:denominator"),
           ("source:out","inputs:a"),("power:power","inputs:b"),("source:out","result:a"),("ratio:out","result:b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(cfg["frames"]==step for cfg in plan.node_configs.values())
    records=execute_array_graph(tmp_path,registry,plan,{"inputs":(count,count),"result":(count,count)},blocks=5)
    f32=lambda value:struct.unpack("<f",struct.pack("<f",value))[0]
    alpha,epsilon=f32(alpha),f32(epsilon);factor=f32(f32(1-alpha)*0.5)
    points=channels*bins;previous=[[0.0]*points for _ in range(2)];last=[[0.0]*points for _ in range(2)]
    different=False
    for (a,b),(original,actual) in zip(records["inputs"],records["result"]):
        assert original==a and a!=b
        if reference is not None:assert actual==reference(a,b)
        values=[struct.unpack(f"<{count}f",data) for data in (a,b)];expected=[]
        for h in range(batches):
            for i in range(points):
                smoothed=[]
                for p in range(2):
                    current=values[p][h*points+i]
                    value=f32(f32(f32(current+previous[p][i])*factor)+f32(last[p][i]*alpha))
                    previous[p][i]=current;last[p][i]=value;smoothed.append(value)
                expected.append(f32(smoothed[0]/f32(smoothed[1]+epsilon)))
        assert actual==struct.pack(f"<{count}f",*expected);different|=actual!=a
    assert different


@pytest.mark.parametrize("channels,bins,batches,alpha,step",[(1,129,1,0.9,128),(1,129,3,0.8,384),(3,5,2,0.5,32)])
def test_trapezoid_ratio_four_entries(tmp_path,channels,bins,batches,alpha,step):
    verify_trapezoid_ratio(tmp_path,channels=channels,bins=bins,batches=batches,alpha=alpha,step=step)
