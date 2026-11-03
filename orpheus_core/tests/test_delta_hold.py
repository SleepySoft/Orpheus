"""真实f32能量到i32标志；严格比较、计数保持与跨批状态。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).parent/"fixtures"


def verify_delta_hold(tmp_path,*,channels=1,batches=1,threshold=1.5,hold_ticks=18,step=128,reference=None):
    registry=Registry([ROOT/"components"]);registry.scan()
    for name in ("array_pair","tensor_pair_capture","mixed_pair_capture"):
        registry.add_manifest(FIXTURES/name/"component.yaml")
    elements=129*batches;count=channels*batches
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",
                      params={"channels":channels,"elements":elements,"mode":0,"complex_mode":1}),
        "power":Node(id="power",component="orpheus.builtin.complex_power",
                     params={"channels":channels,"bins":129,"batches":batches}),
        "detector":Node(id="detector",component="orpheus.builtin.delta_hold",
                        params={"channels":channels,"batches":batches,"threshold":threshold,"hold_ticks":hold_ticks}),
        "discarded":Node(id="discarded",component="orpheus.builtin.delta_hold",
                         params={"channels":channels,"batches":batches,"threshold":threshold,"hold_ticks":hold_ticks}),
        "original":Node(id="original",component="orpheus.test.tensor_pair_capture",
                        params={"channels_a":channels,"channels_b":channels,"frames_a":elements,"frames_b":elements*2}),
        "capture":Node(id="capture",component="orpheus.test.mixed_pair_capture",params={"channels":channels,"frames":batches})}
    pairs=[("source:out","original:a"),("source:complex_out0","original:b"),("source:complex_out0","power:in"),
           ("power:energy","detector:in"),("power:energy","discarded:in"),
           ("power:energy","capture:a"),("detector:out","capture:b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
                    graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==c.get("frame_step",c["frames"])==step for c in plan.node_configs.values())
    assert {b["sample_format"] for b in plan.buffers.values()}=={"f32","i32"}
    blocks=max(8,(hold_ticks+4+batches-1)//batches)
    records=execute_array_graph(tmp_path,registry,plan,{"original":(channels*elements,channels*elements*2),
                                "capture":(count,count)},blocks=blocks)["capture"]
    previous=[0.0]*channels;active=[0]*channels;counters=[0]*channels;seen=set()
    f32=lambda value:struct.unpack("<f",struct.pack("<f",value))[0]
    for source,actual in records:
        if reference is not None:assert actual==reference(source)
        values=struct.unpack(f"<{count}f",source);expected=[]
        for h in range(batches):
            for c in range(channels):
                current=values[h*channels+c]
                if f32(current-previous[c])>threshold:flag=1;counters[c]=0
                elif counters[c]<hold_ticks and active[c]==1:flag=1;counters[c]+=1
                else:flag=0
                previous[c]=current;active[c]=flag;expected.append(flag)
        assert actual==struct.pack(f"<{count}i",*expected);seen.update(expected)
    assert seen=={0,1}, "必须同时验收触发与保持到期，不能只比较全零或一直为1。"


@pytest.mark.parametrize("channels,batches,threshold,hold_ticks,step",[
    (1,1,1.5,18,128),(1,3,6.5,4,384),(3,2,1.5,2,32)])
def test_delta_hold_four_entries(tmp_path,channels,batches,threshold,hold_ticks,step):
    verify_delta_hold(tmp_path,channels=channels,batches=batches,threshold=threshold,hold_ticks=hold_ticks,step=step)
