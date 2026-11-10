"""数据通道拆并不改变处理量子；通道重排用独立预期而非仅比较互逆。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph
from array_interface_fixtures import register_array_interfaces

ROOT=Path(__file__).resolve().parents[2]


def verify_bulk_channels(tmp_path,*,channels,elements,step,order=None):
    registry=Registry([ROOT/"components"]);registry.scan()
    register_array_interfaces(tmp_path, registry)
    if order is None:order=list(reversed(range(channels)))
    assert len(order)==channels and all(c is None or 0<=c<channels for c in order)
    source_port="source:out"
    nodes={
        "source":Node(id="source",component="orpheus.test.array_source",
                      params={"channels":channels,"elements":elements}),
        "split":Node(id="split",component="orpheus.builtin.bulk_deinterleave",params={"channels":channels,"elements":elements}),
        "join":Node(id="join",component="orpheus.builtin.bulk_interleave",params={"channels":channels,"elements":elements}),
        "capture":Node(id="capture",component="orpheus.test.array_capture",
            params={"channels_a":channels,"channels_b":channels,"frames_a":elements,"frames_b":elements})}
    pairs=[(source_port,"split:in"),(source_port,"capture:a"),("join:out","capture:b")]
    pairs += [(f"split:out{c}",f"join:in{i}") for i,c in enumerate(order) if c is not None]
    project=Project(flow_mode="task_sequence",sample_rate=48000,block_size=step,
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==step for c in plan.node_configs.values())
    records=execute_array_graph(tmp_path,registry,plan,{"capture":(channels*elements,channels*elements)},
        calls_per_block=1+sum(c is not None for c in order))["capture"]
    for block,(source,actual) in enumerate(records):
        assert source==struct.pack(f"<{channels*elements}f",
            *[((i*17+block*13)%129-64)*0.03125 for i in range(channels*elements)])
        expected=b"".join(source[(i*channels+c)*4:(i*channels+c+1)*4] if c is not None else bytes(4)
                          for i in range(elements) for c in order)
        assert actual==expected
        assert any(v!=0 for v in struct.unpack(f"<{channels*elements}f",source))


@pytest.mark.parametrize("channels,elements,step,order",[
    (6,258,128,None),(8,774,384,None),(4,129,32,[2,None,0,2]),
    (80,1,32,None),(128,5,32,None)])
def test_bulk_channel_routes_four_entries(tmp_path,channels,elements,step,order):
    verify_bulk_channels(tmp_path,channels=channels,elements=elements,step=step,order=order)
