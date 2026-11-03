"""线性/dB桥的运算顺序与shape独立于调用节拍。"""
import ctypes
from pathlib import Path
import re
import struct
import subprocess
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("channels,elements,step,quantity,epsilon",[
    (1,1,128,0,2.22044605e-16),(3,17,256,1,0.0),(128,1,32,0,0.0001)])
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
    for name in ("array_pair","tensor_pair_capture"):registry.add_manifest(Path(__file__).parent/"fixtures"/name/"component.yaml")
    nodes={"source":Node(id="source",component="orpheus.test.array_pair",params={"channels":channels,"elements":elements,"mode":0})}
    pairs=[]
    for name,mode,upstream in (("forward",0,"source:out"),("inverse",1,"forward:out")):
        nodes[name]=Node(id=name,component="orpheus.builtin.bulk_db",params={"channels":channels,"elements":elements,"mode":mode,"quantity":quantity,"epsilon":epsilon})
        capture=name+"_capture"
        nodes[capture]=Node(id=capture,component="orpheus.test.tensor_pair_capture",params={"channels_a":channels,"frames_a":elements,"channels_b":channels,"frames_b":elements})
        pairs += [(upstream,name+":in"),(upstream,capture+":a"),(name+":out",capture+":b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=step)},
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==step and set(plan.schedule["periods"].values())=={1}
    count=channels*elements
    records=execute_array_graph(tmp_path,registry,plan,{n+"_capture":(count,count) for n in ("forward","inverse")},blocks=12)
    for name,mode in (("forward",0),("inverse",1)):
        for raw,observed in records[name+"_capture"]:
            expected=[library.reference(v,mode,quantity,epsilon) for v in struct.unpack(f"<{count}f",raw)]
            assert observed==struct.pack(f"<{count}f",*expected)
