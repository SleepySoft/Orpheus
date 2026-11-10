"""显式浮点/整数因子两选一，标量latest不改变向量节拍。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import CompileError,GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph
from array_interface_fixtures import register_array_interfaces
from test_interface_delivery import create_component, scalar

ROOT=Path(__file__).resolve().parents[2]

SOURCE=r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_scale_source_algorithm(orpheus_test_scale_source_State* s,
    orpheus_test_scale_source_IO* io,const OrpheusProcessContext* ctx) {
    io->gain.samples[0]=(s->calls%3-1)*0.25f;
    io->integer.samples[0]=(s->calls%3-1)*2;
    io->gain.buffer->frame_count=io->integer.buffer->frame_count=1;
    for(int i=0;i<s->channels*s->elements;i++)
        io->out.samples[i]=((i*17+s->calls*13)%129-64)*0.03125f;
    io->out.buffer->frame_count=(uint32_t)s->elements;
    s->calls++;
    return orpheus_test_scale_source_emit_out(ctx,&io->out);
}
'''


@pytest.mark.parametrize("channels,elements,step",[(1,1,128),(3,17,256),(128,1,32)])
def test_bulk_scale_four_entries(tmp_path,channels,elements,step):
    registry=Registry([ROOT/"components"]);registry.scan()
    register_array_interfaces(tmp_path,registry)
    source=create_component(tmp_path,"scale_source",{
        "out":{"direction":"output","type":"bulk","sample_format":"f32",
               "channels":channels,"block_size":elements,"drive_port_no":1},
        "gain":{"direction":"output","type":"bulk","sample_format":"f32","channels":1,"block_size":1},
        "integer":{"direction":"output","type":"bulk","sample_format":"i32","channels":1,"block_size":1},
        "channels":scalar("int",channels),"elements":scalar("int",elements),"calls":scalar("int",0),
    },SOURCE,source=True)
    registry.add_manifest(source)
    nodes={
        "source":Node(id="source",component="orpheus.test.scale_source")}
    pairs=[]
    for name,gain in (("scaled_float","source:gain"),("scaled_integer","source:integer")):
        nodes[name]=Node(id=name,component="orpheus.builtin.bulk_scale",params={"channels":channels,"elements":elements})
        capture=name+"_capture"
        nodes[capture]=Node(id=capture,component="orpheus.test.array_capture",params={
            "channels_a":channels,"frames_a":elements,"channels_b":channels,"frames_b":elements})
        pairs += [("source:out",name+":in"),(gain,name+(":gain" if name=="scaled_float" else ":integer_gain")),
            ("source:out",capture+":a"),(name+":out",capture+":b")]
    project=Project(flow_mode="task_sequence",sample_rate=48000,block_size=step,
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(plan.node_configs[name]["input_port_timings"][port]=="latest" for name,port in (("scaled_float","gain"),("scaled_integer","integer_gain")))
    count=channels*elements
    records=execute_array_graph(tmp_path,registry,plan,{"scaled_float_capture":(count,count),
        "scaled_integer_capture":(count,count)},blocks=12,calls_per_block=2)
    flags=set()
    for block in range(12):
        factors={"scaled_float":(block%3-1)*0.25,"scaled_integer":float((block%3-1)*2)}
        flags.add(factors["scaled_integer"])
        for name,factor in factors.items():
            raw,observed=records[name+"_capture"][block]
            assert raw==struct.pack(f"<{count}f",*[((i*17+block*13)%129-64)*0.03125 for i in range(count)])
            assert observed==struct.pack(f"<{count}f",*(v*factor for v in struct.unpack(f"<{count}f",raw)))
    assert flags=={-2.0,0.0,2.0}
    project.graph.connections.append(Connection(PortRef.parse("source:gain"),PortRef.parse("scaled_integer:gain")))
    with pytest.raises(CompileError):GraphCompiler(registry).compile(project)
    project.graph.connections.pop()
    project.graph.connections=[connection for connection in project.graph.connections
        if str(connection.to_ref)!="scaled_float:gain"]
    with pytest.raises(CompileError):GraphCompiler(registry).compile(project)
