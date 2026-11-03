"""写完整向量后读取，覆盖实际容量及超过冷启动延迟的四入口采集。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).parent/"fixtures"


def verify_bulk_ring_delay(tmp_path,*,channels=1,bins=129,batches=1,capacity=2193,delay=1161,step=128,reference=None):
    registry=Registry([ROOT/"components"]);registry.scan()
    for name in ("array_pair","tensor_pair_capture"):registry.add_manifest(FIXTURES/name/"component.yaml")
    elements=bins*batches;count=channels*elements
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",params={"channels":channels,"elements":elements,"mode":0}),
        "delay":Node(id="delay",component="orpheus.builtin.bulk_ring_delay",
                     params={"channels":channels,"bins":bins,"batches":batches,
                             "capacity_elements":capacity,"delay_elements":delay}),
        "capture":Node(id="capture",component="orpheus.test.tensor_pair_capture",
                       params={"channels_a":channels,"channels_b":channels,"frames_a":elements,"frames_b":elements})}
    pairs=[("source:out","delay:in"),("source:out","capture:a"),("delay:out","capture:b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
                    graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==c.get("frame_step",c["frames"])==step for c in plan.node_configs.values())
    assert all(b["type"]=="bulk" for b in plan.buffers.values())
    blocks=max(8,(capacity*2+elements-1)//elements)
    records=execute_array_graph(tmp_path,registry,plan,{"capture":(count,count)},blocks=blocks)["capture"]
    history=[bytes(4)]*(capacity*channels);cursor=0;nonzero=False
    for block,(source,actual) in enumerate(records):
        assert source==struct.pack(f"<{count}f",*[((i*17+block*13)%129-64)*.03125 for i in range(count)])
        if reference is not None:assert actual==reference(source)
        expected=[]
        for h in range(batches):
            for k in range(bins):
                for c in range(channels):
                    i=((h*bins+k)*channels+c)*4
                    history[((cursor+k)%capacity)*channels+c]=source[i:i+4]
            for k in range(bins):
                for c in range(channels):expected.append(history[((cursor-delay+k)%capacity)*channels+c])
            cursor=(cursor+bins)%capacity
        assert actual==b"".join(expected)
        nonzero |= any(actual)
    assert nonzero, "验收必须实际读出历史，不能只比较冷启动全零。"


@pytest.mark.parametrize("channels,bins,batches,capacity,delay,step",[
    (1,129,1,2193,1161,128),(1,129,3,774,258,384),(3,7,2,11,9,32),(2,7,2,11,0,32),(2,7,2,11,34,32)])
def test_bulk_ring_delay_four_entries(tmp_path,channels,bins,batches,capacity,delay,step):
    verify_bulk_ring_delay(tmp_path,channels=channels,bins=bins,batches=batches,capacity=capacity,delay=delay,step=step)
