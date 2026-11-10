"""同shape逐元素历史；不将数据行数当作音频时钟。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph
from array_interface_fixtures import register_array_interfaces

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("channels,elements,step,alpha,absolute,update",[
    (3,1,256,0.83,True,False),(2,129,128,0.61,False,True),
    (1,387,384,0.37,True,True),(128,1,32,0.0,False,True),(1,7,8,1.0,False,True)])
def test_bulk_one_pole_four_entries(tmp_path,channels,elements,step,alpha,absolute,update):
    registry=Registry([ROOT/"components"]);registry.scan()
    register_array_interfaces(tmp_path,registry,scalar_signals=update)
    nodes={
        "source":Node(id="source",component="orpheus.test.array_source",params={"channels":channels,"elements":elements}),
        "smooth":Node(id="smooth",component="orpheus.builtin.bulk_one_pole",params={
            "channels":channels,"elements":elements,"alpha":alpha,"absolute_input":absolute}),
        "capture":Node(id="capture",component="orpheus.test.array_capture",params={
            "channels_a":channels,"frames_a":elements,"channels_b":channels,"frames_b":elements})}
    pairs=[("source:out","smooth:in"),("source:out","capture:a"),("smooth:out","capture:b")]
    if update:pairs.append(("source:update","smooth:update"))
    project=Project(flow_mode="task_sequence",sample_rate=48000,block_size=step,
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    count=channels*elements
    records=execute_array_graph(tmp_path,registry,plan,{"capture":(count,count)},blocks=12,calls_per_block=2)["capture"]
    def f32(value):return struct.unpack("<f",struct.pack("<f",value))[0]
    alpha=f32(alpha);factor=f32(1-alpha);history=[0.0]*count
    for block,(raw,actual) in enumerate(records):
        assert raw==struct.pack(f"<{count}f",*[((i*17+block*13)%129-64)*0.03125 for i in range(count)])
        values=struct.unpack(f"<{count}f",raw)
        if not update or block%3!=1:
            history=[f32(f32(factor*(abs(x) if absolute else x))+f32(h*alpha)) for x,h in zip(values,history)]
        assert actual==struct.pack(f"<{count}f",*history)
