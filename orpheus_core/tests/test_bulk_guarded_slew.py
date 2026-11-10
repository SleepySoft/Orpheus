"""同shape三份独立历史、可选边界标记及四入口精确比较。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph
from array_interface_fixtures import register_array_interfaces

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("channels,elements,step,bound",[(1,1,128,False),(3,17,256,True),(128,1,32,True)])
def test_bulk_guarded_slew_four_entries(tmp_path,channels,elements,step,bound):
    registry=Registry([ROOT/"components"]);registry.scan()
    register_array_interfaces(tmp_path,registry,scalar_signals=bound)
    parameters={"channels":channels,"elements":elements,"attack":0.3,"decay":-0.4,"threshold":0.65,"slow":0.07,"initial":0.125}
    nodes={
        "source":Node(id="source",component="orpheus.test.array_source",params={"channels":channels,"elements":elements}),
        "slew":Node(id="slew",component="orpheus.builtin.bulk_guarded_slew",params=parameters),
        "capture":Node(id="capture",component="orpheus.test.array_capture",params={
            "channels_a":channels,"frames_a":elements,"channels_b":channels,"frames_b":elements})}
    pairs=[("source:out","slew:in"),("source:out","capture:a"),("slew:out","capture:b")]
    if bound:
        pairs.append(("source:bound","slew:bound"))
    project=Project(flow_mode="task_sequence",sample_rate=48000,block_size=step,
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    count=channels*elements
    records=execute_array_graph(tmp_path,registry,plan,{"capture":(count,count)},blocks=12,calls_per_block=2)["capture"]
    def f32(value):return struct.unpack("<f",struct.pack("<f",value))[0]
    attack,decay,threshold,slow,initial=[f32(parameters[k]) for k in ("attack","decay","threshold","slow","initial")]
    previous=[initial]*count;deltas=[0.0]*count;changes=[0.0]*count
    for block,(raw,actual) in enumerate(records):
        assert raw==struct.pack(f"<{count}f",*[((i*17+block*13)%129-64)*0.03125 for i in range(count)])
        values=struct.unpack(f"<{count}f",raw);flag=bound and block%3!=1
        expected=[]
        for i,x in enumerate(values):
            delta=f32(x-previous[i])
            change=min(attack,delta) if delta>0 else max(decay,delta)
            slew=changes[i] if f32(deltas[i]*delta)>0 else 0.0
            if flag and slew==0:slew=min(slow,delta) if delta>0 else max(-slow,delta)
            if abs(delta)<=threshold and abs(slew)<abs(change):change=slew
            previous[i]=f32(previous[i]+change);deltas[i]=delta;changes[i]=change;expected.append(previous[i])
        assert actual==struct.pack(f"<{count}f",*expected)
