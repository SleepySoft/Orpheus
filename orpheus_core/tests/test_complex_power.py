"""复数功率及逐批能量，所有数组都不充当音频时间长度。"""
import math
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent/"fixtures"


def verify_complex_power(tmp_path,*,channels=1,bins=129,batches=1,step=128,reference=None):
    registry = Registry([ROOT/"components"]);registry.scan()
    for name in ("array_pair","tensor_pair_capture"):
        registry.add_manifest(FIXTURES/name/"component.yaml")
    elements = bins*batches;count = channels*elements
    nodes = {
        "source":Node(id="source",component="orpheus.test.array_pair",
                      params={"channels":channels,"elements":elements,"mode":0,"complex_mode":1}),
        "power":Node(id="power",component="orpheus.builtin.complex_power",
                     params={"channels":channels,"bins":bins,"batches":batches}),
        "original":Node(id="original",component="orpheus.test.array_pair",
                        params={"channels":channels,"elements":elements,"mode":1,"complex_mode":1})}
    pairs = [("source:out","original:a"),("source:complex_out0","original:complex_in0"),
             ("source:complex_out0","power:in")]
    for name,frames in (("powers",elements),("energies",batches)):
        nodes[name] = Node(id=name,component="orpheus.test.tensor_pair_capture",
                           params={"channels_a":channels,"channels_b":channels,"frames_a":elements*2,"frames_b":frames})
        pairs += [("source:complex_out0",name+":a"),("power:"+("power" if name=="powers" else "energy"),name+":b")]
    project = Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
                      graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan = GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==c.get("frame_step",c["frames"])==step for c in plan.node_configs.values())
    assert all(b["type"]=="bulk" for b in plan.buffers.values())
    records = execute_array_graph(tmp_path,registry,plan,{"original":(count,count*2),
                                  "powers":(count*2,count),"energies":(count*2,batches*channels)})
    f32 = lambda value:struct.unpack("<f",struct.pack("<f",value))[0]
    for block in range(3):
        original = records["original"][block][1]
        assert original==struct.pack(f"<{count*2}f",*[((i*29+block*7)%257-128)*.0078125 for i in range(count*2)])
        assert records["powers"][block][0]==records["energies"][block][0]==original
        powers,energies = records["powers"][block][1],records["energies"][block][1]
        if reference is not None:
            want_p,want_e = reference(original)
            assert powers==want_p and energies==want_e
        else:
            values = struct.unpack(f"<{count*2}f",original);expected = []
            for point in range(elements):
                for c in range(channels):
                    i=point*channels*2+c
                    expected.append(f32(f32(values[i]*values[i])+f32(values[i+channels]*values[i+channels])))
            assert powers==struct.pack(f"<{count}f",*expected)
            actual_e = struct.unpack(f"<{batches*channels}f",energies)
            for h in range(batches):
                for c in range(channels):
                    total=-0.0
                    for k in range(bins):total=f32(total+expected[(h*bins+k)*channels+c])
                    wanted=f32(f32(math.log10(f32(total+f32(2.22044605e-16))))*10)
                    assert abs(actual_e[h*channels+c]-wanted)<=1e-5


@pytest.mark.parametrize("channels,bins,batches,step",[(1,129,1,128),(1,129,3,384),(3,7,2,32)])
def test_complex_power_four_entries(tmp_path,channels,bins,batches,step):
    verify_complex_power(tmp_path,channels=channels,bins=bins,batches=batches,step=step)
