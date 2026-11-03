"""显式浮点/整数因子两选一，标量latest不改变向量节拍。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import CompileError,GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("channels,elements,step",[(1,1,128),(3,17,256),(128,1,32)])
def test_bulk_scale_four_entries(tmp_path,channels,elements,step):
    registry=Registry([ROOT/"components"]);registry.scan()
    for name in ("array_pair","tensor_pair_capture","mixed_pair_capture"):
        registry.add_manifest(Path(__file__).parent/"fixtures"/name/"component.yaml")
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",params={"channels":channels,"elements":elements,"mode":0}),
        "gain":Node(id="gain",component="orpheus.test.array_pair",params={"channels":1,"elements":1,"mode":0}),
        "flag":Node(id="flag",component="orpheus.builtin.delta_hold",params={"channels":1,"batches":1,"threshold":0.0,"hold_ticks":0}),
        "gain_capture":Node(id="gain_capture",component="orpheus.test.mixed_pair_capture",params={"channels":1,"frames":1})}
    pairs=[("gain:out","flag:in"),("gain:out","gain_capture:a"),("flag:out","gain_capture:b")]
    for name,gain in (("scaled_float","gain:out"),("scaled_integer","flag:out")):
        nodes[name]=Node(id=name,component="orpheus.builtin.bulk_scale",params={"channels":channels,"elements":elements})
        capture=name+"_capture"
        nodes[capture]=Node(id=capture,component="orpheus.test.tensor_pair_capture",params={
            "channels_a":channels,"frames_a":elements,"channels_b":channels,"frames_b":elements})
        pairs += [("source:out",name+":in"),(gain,name+(":gain" if name=="scaled_float" else ":integer_gain")),
            ("source:out",capture+":a"),(name+":out",capture+":b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(plan.node_configs[name]["input_port_timings"][port]=="latest" for name,port in (("scaled_float","gain"),("scaled_integer","integer_gain")))
    count=channels*elements
    records=execute_array_graph(tmp_path,registry,plan,{"gain_capture":(1,1),"scaled_float_capture":(count,count),"scaled_integer_capture":(count,count)},blocks=12)
    flags=set()
    for block,(gain,integer) in enumerate(records["gain_capture"]):
        factors={"scaled_float":struct.unpack("<f",gain)[0],"scaled_integer":float(struct.unpack("<i",integer)[0])};flags.add(factors["scaled_integer"])
        for name,factor in factors.items():
            raw,observed=records[name+"_capture"][block]
            assert observed==struct.pack(f"<{count}f",*(v*factor for v in struct.unpack(f"<{count}f",raw)))
    assert flags=={0.0,1.0}
    project.graph.connections.append(Connection(PortRef.parse("gain:out"),PortRef.parse("scaled_integer:gain")))
    with pytest.raises(CompileError):GraphCompiler(registry).compile(project)
