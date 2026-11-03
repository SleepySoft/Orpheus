"""数据通道拆并不改变处理量子；通道重排用独立预期而非仅比较互逆。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]


def verify_bulk_channels(tmp_path,*,channels,elements,step,order=None):
    registry=Registry([ROOT/"components"]);registry.scan()
    fixtures=Path(__file__).parent/"fixtures"
    for name in ("array_pair","tensor_pair_capture"):registry.add_manifest(fixtures/name/"component.yaml")
    if order is None:order=list(reversed(range(channels)))
    assert len(order)==channels and all(c is None or 0<=c<channels for c in order)
    complex_mode=int(elements%2==0)
    source_elements=elements//2 if complex_mode else elements
    source_port="source:complex_out0" if complex_mode else "source:out"
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",
                      params={"channels":channels,"elements":source_elements,"mode":0,"complex_mode":complex_mode}),
        "split":Node(id="split",component="orpheus.builtin.bulk_deinterleave",params={"channels":channels,"elements":elements}),
        "join":Node(id="join",component="orpheus.builtin.bulk_interleave",params={"channels":channels,"elements":elements}),
        "capture":Node(id="capture",component="orpheus.test.tensor_pair_capture",
            params={"channels_a":channels,"channels_b":channels,"frames_a":elements,"frames_b":elements})}
    pairs=[(source_port,"split:in"),(source_port,"capture:a"),("join:out","capture:b")]
    pairs += [(f"split:out{c}",f"join:in{i}") for i,c in enumerate(order) if c is not None]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==step for c in plan.node_configs.values())
    records=execute_array_graph(tmp_path,registry,plan,{"capture":(channels*elements,channels*elements)})["capture"]
    for source,actual in records:
        expected=b"".join(source[(i*channels+c)*4:(i*channels+c+1)*4] if c is not None else bytes(4)
                          for i in range(elements) for c in order)
        assert actual==expected
        assert any(v!=0 for v in struct.unpack(f"<{channels*elements}f",source))


@pytest.mark.parametrize("channels,elements,step,order",[
    (6,258,128,None),(8,774,384,None),(4,129,32,[2,None,0,2]),
    (80,1,32,None),(128,5,32,None)])
def test_bulk_channel_routes_four_entries(tmp_path,channels,elements,step,order):
    verify_bulk_channels(tmp_path,channels=channels,elements=elements,step=step,order=order)
