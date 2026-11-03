"""实增益作用于复数，并由原复数减去已舍入乘积；不是(1-g)*x。"""
from pathlib import Path
import struct
import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).parent / "fixtures/array_pair"


def verify_complex_gain_split(tmp_path, *, channels=1, elements=129, step=128, reference=None):
    registry = Registry([ROOT / "components"]); registry.scan()
    registry.add_manifest(FIXTURE / "component.yaml")
    nodes = {
        "source": Node(id="source",component="orpheus.test.array_pair",
                       params={"channels": channels,"elements": elements,"mode": 0,"complex_mode": 1}),
        "split": Node(id="split",component="orpheus.builtin.complex_gain_split",
                      params={"channels": channels,"elements": elements}),
    }
    pairs = [("source:out","split:gain"),("source:complex_out0","split:in")]
    for name, output in (("original","source:complex_out0"),("impulse","split:impulse"),("reverb","split:reverb")):
        nodes[name] = Node(id=name,component="orpheus.test.array_pair",
                           params={"channels": channels,"elements": elements,"mode": 1,"complex_mode": 1})
        pairs += [("source:out",name+":a"),(output,name+":complex_in0")]
    project = Project(tasks={"default": Task(id="default",sample_rate=48000,block_size=step)},
                      graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan = GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==c.get("frame_step",c["frames"])==step for c in plan.node_configs.values())
    assert all(b["type"]=="bulk" and b["frame_count"] in (elements,elements*2) for b in plan.buffers.values())
    count = channels*elements
    records = execute_array_graph(tmp_path,registry,plan,{name:(count,count*2) for name in ("original","impulse","reverb")})
    f32 = lambda value: struct.unpack("<f",struct.pack("<f",value))[0]
    for block in range(3):
        gain, original = records["original"][block]
        assert gain==struct.pack(f"<{count}f",*[((i*17+block*13)%129-64)*.03125 for i in range(count)])
        assert original==struct.pack(f"<{count*2}f",*[((i*29+block*7)%257-128)*.0078125 for i in range(count*2)])
        assert records["impulse"][block][0]==records["reverb"][block][0]==gain
        if reference is not None:
            expected_impulse,expected_reverb = reference(original,gain)
        else:
            values = struct.unpack(f"<{count*2}f",original); gains = struct.unpack(f"<{count}f",gain)
            scaled = [f32(gains[(i//(channels*2))*channels+i%channels]*x) for i,x in enumerate(values)]
            expected_reverb = struct.pack(f"<{count*2}f",*scaled)
            expected_impulse = struct.pack(f"<{count*2}f",*[f32(x-y) for x,y in zip(values,scaled)])
        assert records["impulse"][block][1]==expected_impulse
        assert records["reverb"][block][1]==expected_reverb


@pytest.mark.parametrize("channels,elements,step",[(1,129,128),(1,387,384),(3,7,32)])
def test_complex_gain_split_four_entries(tmp_path,channels,elements,step):
    verify_complex_gain_split(tmp_path,channels=channels,elements=elements,step=step)
