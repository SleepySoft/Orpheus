"""向量梯形递推的批内推进和跨块历史，不以普通EMA替代。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent/"fixtures"


def verify_dual_trapezoid_smooth(tmp_path,*,channels=1,bins=129,batches=1,step=128,
                                fast_alpha=0.5,slow_alpha=0.95,reference=None):
    registry = Registry([ROOT/"components"]);registry.scan()
    for name in ("array_pair","tensor_pair_capture"):
        registry.add_manifest(FIXTURES/name/"component.yaml")
    elements=bins*batches;count=channels*elements
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",params={"channels":channels,"elements":elements,"mode":0}),
        "smooth":Node(id="smooth",component="orpheus.builtin.dual_trapezoid_smooth",
                      params={"channels":channels,"bins":bins,"batches":batches,
                              "fast_alpha":fast_alpha,"slow_alpha":slow_alpha})}
    pairs=[("source:out","smooth:in")]
    for name in ("fast","slow"):
        nodes[name]=Node(id=name,component="orpheus.test.tensor_pair_capture",
                         params={"channels_a":channels,"channels_b":channels,"frames_a":elements,"frames_b":elements})
        pairs += [("source:out",name+":a"),("smooth:"+name,name+":b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
                    graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==c.get("frame_step",c["frames"])==step for c in plan.node_configs.values())
    assert all(b["type"]=="bulk" for b in plan.buffers.values())
    records=execute_array_graph(tmp_path,registry,plan,{"fast":(count,count),"slow":(count,count)})
    f32=lambda value:struct.unpack("<f",struct.pack("<f",value))[0]
    points=channels*bins;previous=[0.0]*points;history=[[0.0]*points for _ in range(2)]
    alphas=[f32(fast_alpha),f32(slow_alpha)];factors=[f32(f32(1-alpha)*.5) for alpha in alphas]
    for block in range(3):
        source=records["fast"][block][0]
        assert source==records["slow"][block][0]
        assert source==struct.pack(f"<{count}f",*[((i*17+block*13)%129-64)*.03125 for i in range(count)])
        actual=[records[name][block][1] for name in ("fast","slow")]
        if reference is not None:
            assert tuple(actual)==reference(source)
        else:
            values=struct.unpack(f"<{count}f",source);expected=[[],[]]
            for h in range(batches):
                for i in range(points):
                    current=values[h*points+i];pair=f32(current+previous[i])
                    for p in range(2):
                        value=f32(f32(pair*factors[p])+f32(history[p][i]*alphas[p]))
                        expected[p].append(value);history[p][i]=value
                    previous[i]=current
            assert actual==[struct.pack(f"<{count}f",*values) for values in expected]


@pytest.mark.parametrize("channels,bins,batches,step",[(1,129,1,128),(1,129,3,384),(3,7,2,32)])
def test_dual_trapezoid_smooth_four_entries(tmp_path,channels,bins,batches,step):
    verify_dual_trapezoid_smooth(tmp_path,channels=channels,bins=bins,batches=batches,step=step)
