"""线性步进切片；大矩阵由已有拆并桥构造，不扩大共享采集器。"""
from pathlib import Path
import struct
import pytest
from orpheus_core.compiler import CompileError,GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph
from array_interface_fixtures import register_array_interfaces

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("input_channels,input_elements,channels,elements,offset,stride,step",[
    (3,5,2,3,2,2,32),(2,258,2,60,20,1,256),(100,130,1,65,99,200,128)])
def test_bulk_slice_four_entries(tmp_path,input_channels,input_elements,channels,elements,offset,stride,step):
    registry=Registry([ROOT/"components"]);registry.scan()
    register_array_interfaces(tmp_path, registry)
    padded=input_channels*input_elements>8192
    source_channels=input_channels//2 if padded else input_channels
    source_port="source:out"
    nodes={
        "source":Node(id="source",component="orpheus.test.array_source",params={
            "channels":source_channels,"elements":input_elements}),
        "slice":Node(id="slice",component="orpheus.builtin.bulk_slice",params={
            "input_channels":input_channels,"input_elements":input_elements,"channels":channels,
            "elements":elements,"offset":offset,"stride":stride}),
        "capture":Node(id="capture",component="orpheus.test.array_capture",params={
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
    project=Project(flow_mode="task_sequence",sample_rate=48000,block_size=step,
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert [edge["node"] for edge in plan.call_graph["source"]]==[
        "capture", "split" if padded else "slice"]
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    assert all(c["frames"]==step for c in plan.node_configs.values())
    captures={"capture":(source_channels*input_elements,channels*elements)}
    records=execute_array_graph(tmp_path,registry,plan,captures,
        calls_per_block=1+(source_channels if padded else 1))["capture"]
    for block,(source,actual) in enumerate(records):
        source_count=source_channels*input_elements
        assert source==struct.pack(f"<{source_count}f",
            *[((i*17+block*13)%129-64)*0.03125 for i in range(source_count)])
        if padded:
            row_bytes=source_channels*4
            source=b"".join(bytes(row_bytes)+source[i:i+row_bytes] for i in range(0,len(source),row_bytes))
        expected=b"".join(source[(offset+i*stride)*4:(offset+i*stride+1)*4] for i in range(channels*elements))
        assert actual==expected and any(actual)
    assert len({actual for _,actual in records})==len(records)


@pytest.mark.parametrize("consumer_first", [False, True])
def test_data_only_input_still_requires_producer_before_call(tmp_path, consumer_first):
    registry=Registry([ROOT/"components"]);registry.scan()
    register_array_interfaces(tmp_path, registry, capture_b_process=False)
    branches=[("source:out","slice:in"),("source:out","capture:a")]
    if consumer_first:
        branches.reverse()
    project=Project(flow_mode="task_sequence",block_size=8,graph=Graph(
        nodes={
            "source":Node("source","orpheus.test.array_source",params={"channels":1,"elements":4}),
            "slice":Node("slice","orpheus.builtin.bulk_slice",params={
                "input_channels":1,"input_elements":4,"channels":1,"elements":4}),
            "capture":Node("capture","orpheus.test.array_capture",params={
                "channels_a":1,"channels_b":1,"frames_a":4,"frames_b":4}),
        },
        connections=[Connection(PortRef.parse(a),PortRef.parse(b))
                     for a,b in [*branches,("slice:out","capture:b")]],
    ))
    if consumer_first:
        with pytest.raises(CompileError,match="顺序未声明"):
            GraphCompiler(registry).compile(project)
    else:
        plan=GraphCompiler(registry).compile(project)
        assert [edge["node"] for edge in plan.call_graph["source"]]==["slice","capture"]
        assert plan.call_graph["slice"]==[]
