"""线性步进切片；大矩阵由已有拆并桥构造，不扩大共享采集器。"""
from pathlib import Path
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("input_channels,input_elements,channels,elements,offset,stride,step",[
    (3,5,2,3,2,2,32),(2,258,2,60,20,1,256),(100,130,1,65,99,200,128)])
def test_bulk_slice_four_entries(tmp_path,input_channels,input_elements,channels,elements,offset,stride,step):
    registry=Registry([ROOT/"components"]);registry.scan()
    fixtures=Path(__file__).parent/"fixtures"
    for name in ("array_pair","tensor_pair_capture"):registry.add_manifest(fixtures/name/"component.yaml")
    padded=input_channels*input_elements>8192
    source_channels=input_channels//2 if padded else input_channels
    complex_mode=int(input_elements%2==0)
    source_elements=input_elements//2 if complex_mode else input_elements
    source_port="source:complex_out0" if complex_mode else "source:out"
    nodes={
        "source":Node(id="source",component="orpheus.test.array_pair",params={
            "channels":source_channels,"elements":source_elements,"mode":0,"complex_mode":complex_mode}),
        "slice":Node(id="slice",component="orpheus.builtin.bulk_slice",params={
            "input_channels":input_channels,"input_elements":input_elements,"channels":channels,
            "elements":elements,"offset":offset,"stride":stride}),
        "capture":Node(id="capture",component="orpheus.test.tensor_pair_capture",params={
            "channels_a":source_channels,"frames_a":input_elements,"channels_b":channels,"frames_b":elements})}
    pairs=[(source_port,"capture:a"),("slice:out","capture:b")]
    if padded:
        nodes["split"]=Node(id="split",component="orpheus.builtin.bulk_deinterleave",
                            params={"channels":source_channels,"elements":input_elements})
        nodes["join"]=Node(id="join",component="orpheus.builtin.bulk_interleave",
                           params={"channels":input_channels,"elements":input_elements})
        pairs += [(source_port,"split:in"),("join:out","slice:in")]
        pairs += [(f"split:out{c}",f"join:in{c+source_channels}") for c in range(source_channels)]
    else:
        pairs.append((source_port,"slice:in"))
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==step for c in plan.node_configs.values())
    captures={"capture":(source_channels*input_elements,channels*elements)}
    records=execute_array_graph(tmp_path,registry,plan,captures)["capture"]
    for source,actual in records:
        if padded:
            row_bytes=source_channels*4
            source=b"".join(bytes(row_bytes)+source[i:i+row_bytes] for i in range(0,len(source),row_bytes))
        expected=b"".join(source[(offset+i*stride)*4:(offset+i*stride+1)*4] for i in range(channels*elements))
        assert actual==expected and any(actual)
    assert len({actual for _,actual in records})==len(records)
