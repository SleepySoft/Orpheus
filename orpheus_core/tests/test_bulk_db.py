"""线性/dB桥的运算顺序与shape独立于调用节拍。"""
import ctypes
from pathlib import Path
import re
import struct
import subprocess
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph
from array_interface_fixtures import register_array_interfaces

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("channels,elements,step,quantity,epsilon",[
    (1,1,128,0,2.22044605e-16),(3,17,256,1,0.0),(128,1,32,0,0.0001),
    (1,3,8,1,float.fromhex("0x1.fffffep127"))])
def test_bulk_db_four_entries(tmp_path,channels,elements,step,quantity,epsilon):
    source=tmp_path/"oracle.c";dll=tmp_path/"oracle.dll"
    source.write_text("#include <math.h>\n__declspec(dllexport) float reference(float x,int mode,int quantity,float epsilon){"
        "float factor=quantity ? 20.0f : 10.0f;if(mode){volatile float v=x*2.30258512f;v=v/factor;return expf(v);}"
        "volatile float v=x+epsilon;return log10f(v)*factor;}\n",encoding="utf-8",newline="\n")
    compiler=re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$",(ROOT/"build/CMakeCache.txt").read_text(encoding="utf-8"),re.M)[1]
    result=subprocess.run([compiler,"-shared","-std=c11",str(source),"-o",str(dll),"-lm"],capture_output=True,text=True,encoding="utf-8")
    assert result.returncode==0,result.stdout+result.stderr
    library=ctypes.CDLL(str(dll));library.reference.argtypes=[ctypes.c_float,ctypes.c_int,ctypes.c_int,ctypes.c_float];library.reference.restype=ctypes.c_float
    registry=Registry([ROOT/"components"]);registry.scan()
    register_array_interfaces(tmp_path,registry)
    nodes={"source":Node(id="source",component="orpheus.test.array_source",params={"channels":channels,"elements":elements})}
    pairs=[]
    for name,mode,upstream in (("forward",0,"source:out"),("inverse",1,"forward:out")):
        nodes[name]=Node(id=name,component="orpheus.builtin.bulk_db",params={"channels":channels,"elements":elements,"mode":mode,"quantity":quantity,"epsilon":epsilon})
        capture=name+"_capture"
        nodes[capture]=Node(id=capture,component="orpheus.test.array_capture",params={"channels_a":channels,"frames_a":elements,"channels_b":channels,"frames_b":elements})
        pairs += [(upstream,name+":in"),(upstream,capture+":a"),(name+":out",capture+":b")]
    project=Project(flow_mode="task_sequence",sample_rate=48000,block_size=step,
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    count=channels*elements
    records=execute_array_graph(tmp_path,registry,plan,{n+"_capture":(count,count) for n in ("forward","inverse")},
        blocks=12,calls_per_block=2)
    for name,mode in (("forward",0),("inverse",1)):
        for block,(raw,observed) in enumerate(records[name+"_capture"]):
            source_values=[((i*17+block*13)%129-64)*0.03125 for i in range(count)]
            reference_input=source_values if not mode else [
                library.reference(value,0,quantity,epsilon) for value in source_values]
            assert raw==struct.pack(f"<{count}f",*reference_input)
            expected=[library.reference(v,mode,quantity,epsilon) for v in struct.unpack(f"<{count}f",raw)]
            assert observed==struct.pack(f"<{count}f",*expected)
