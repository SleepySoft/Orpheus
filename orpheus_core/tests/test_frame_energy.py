"""音频固定窗口归约为同步数据，不平均、广播或跨块累积。"""
from pathlib import Path
import struct
import wave
import pytest
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection,Graph,Node,PortRef,Project,Task
from orpheus_core.registry import Registry
from array_graph_runner import execute_array_graph

ROOT=Path(__file__).resolve().parents[2]


def f32(value):return struct.unpack("<f",struct.pack("<f",value))[0]


@pytest.mark.parametrize("channels,frames",[(1,1),(2,128),(64,17)])
def test_frame_energy_four_entries(tmp_path,channels,frames):
    samples=[((n*(113+c*137)+c*911)%50001)-25000 for n in range(frames*12) for c in range(channels)]
    with wave.open(str(tmp_path/"input.wav"),"wb") as stream:
        stream.setnchannels(channels);stream.setsampwidth(2);stream.setframerate(48000)
        stream.writeframes(struct.pack(f"<{len(samples)}h",*samples))
    registry=Registry([ROOT/"components"]);registry.scan()
    registry.add_manifest(Path(__file__).parent/"fixtures/tensor_pair_capture/component.yaml")
    nodes={
        "source":Node(id="source",component="orpheus.builtin.wav_in",params={"channels":channels,"file_path":"input.wav"}),
        "energy":Node(id="energy",component="orpheus.builtin.frame_energy",params={"channels":channels,"frames":frames}),
        "capture":Node(id="capture",component="orpheus.test.tensor_pair_capture",params={"channels_a":1,"frames_a":1,"channels_b":1,"frames_b":1})}
    pairs=[("source:out","energy:in"),("energy:out","capture:a"),("energy:out","capture:b")]
    project=Project(tasks={"default":Task(id="default",sample_rate=48000,block_size=frames)},
        graph=Graph(nodes=nodes,connections=[Connection(PortRef.parse(a),PortRef.parse(b)) for a,b in pairs]))
    plan=GraphCompiler(registry).compile(project)
    assert plan.schedule["tick"]==frames and set(plan.schedule["periods"].values())=={1}
    buffer=next(b for b in plan.buffers.values() if b["from"]=="energy:out")
    assert (buffer["type"],buffer["channels"],buffer["frame_count"])==("bulk",1,1)
    records=execute_array_graph(tmp_path,registry,plan,{"capture":(1,1)},blocks=12)["capture"]
    for block,(a,b) in enumerate(records):
        total=-0.0
        for frame in range(frames):
            values=[samples[((block*frames+frame)*channels)+c]/32768.0 for c in range(channels)]
            row=f32(values[0]*values[0])
            for value in values[1:]:row=f32(row+f32(value*value))
            total=f32(total+row)
        assert a==b==struct.pack("<f",total)
    assert len({b for _,b in records})==12
