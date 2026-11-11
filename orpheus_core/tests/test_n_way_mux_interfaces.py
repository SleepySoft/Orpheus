"""多入口真实交付不合并：逐位选择与跨缺失输入的位置渐变。"""
import struct
import wave

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from orpheus_core.subgraph import flatten_project
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_limiter_interfaces import CAPTURE
from test_table_interfaces import f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_mux_source_algorithm(orpheus_test_mux_source_State* s,
    orpheus_test_mux_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned i=0;i<io->out_count;i++) if(io->out[i].buffer) {
            for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++)
                io->out[i].samples[n*@CHANNELS@+c]=
                    (float)((int)(((s->calls*@FRAMES@+n)*17+c*31+i*23)%103)-51)*.0078125f;
        }
        int rc=orpheus_test_mux_source_emit_out(ctx,&io->out[io->out_count-1]);
        if(rc) return rc;
        rc=orpheus_test_mux_source_emit_out(ctx,&io->out[0]);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''

def test_named_drive_input_survives_project_flattening():
    registry = Registry()
    for name in ("wav_in", "n_way_mux", "wav_out"):
        registry.add_manifest(ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml")
    connections = [
        Connection(PortRef("source", "out"), PortRef("mux", "in1")),
        Connection(PortRef("mux", "out"), PortRef("sink", "in")),
    ]
    project = Project(flow_mode="task_sequence", graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.wav_in", params={"channels": 1}),
        "mux": Node("mux", "orpheus.builtin.n_way_mux", params={"channels": 1}),
        "sink": Node("sink", "orpheus.builtin.wav_out", params={"channels": 1}),
    }, connections=connections), drive_connections=[
        DriveConnection(edge.from_ref, edge.to_ref) for edge in connections
    ])
    flat = flatten_project(project, registry)
    assert flat.drive_connections == project.drive_connections
    plan = GraphCompiler(registry).compile(flat)
    assert plan.call_graph["source"][0]["target_interface"] == "in1"
    assert plan.call_graph["source"][0]["input_port"] == 1


@pytest.mark.parametrize("inputs,channels,frames,rate,ramp_ms", [
    (32, 64, 7, 8000, 0), (3, 3, 257, 48000, 8),
])
def test_each_input_advances_and_emits_without_waiting(tmp_path, inputs, channels, frames, rate, ramp_ms):
    step = f32(1 / f32(f32(f32(ramp_ms) / 1000) * rate)) if ramp_ms else 32.0
    expected, pcm, pos = [], [], 0.0
    for call in range(8):
        target = float(inputs - 1 if call < 4 else 0)
        if step >= 32:
            pos = target
        steady = pos == target
        for n in range(frames):
            if not steady:
                pos = min(target, f32(pos + step)) if pos < target else max(target, f32(pos - step))
            a, b = int(pos), int(pos) + 1
            fraction = f32(pos - a)
            if b >= inputs:
                b, fraction = a, 0.0
            for c in range(channels):
                def sample(index):
                    return f32((((call // 2 * frames + n) * 17 + c * 31 + index * 23) % 103 - 51) * .0078125)
                if steady:
                    value = sample(a) if a in (0, inputs - 1) else 0.0
                else:
                    pa, pb = a in (0, inputs - 1), b in (0, inputs - 1)
                    left = f32(sample(a) * f32(1 - fraction)) if pa else 0.0
                    right = f32(sample(b) * fraction) if pb else 0.0
                    value = f32(left + right) if pa and pb else left if pa else right
                expected.append(value)
                pcm.append(int(f32(value * 32767)))
    source = create_component(tmp_path, "mux_source", {
        "out": audio("output", channels=channels, count=inputs, drive_port_no=1),
        "calls": scalar("int", 0),
    }, SOURCE.replace("@FRAMES@", str(frames)).replace("@CHANNELS@", str(channels)), source=True)
    capture_code = CAPTURE.replace("limiter_capture", "mux_capture").replace(
        "s->calls>=4", "s->calls>=8").replace("s->calls/2", "s->calls/4")
    capture = create_component(tmp_path, "mux_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, capture_code.replace("@FRAMES@", str(frames)).replace("@COUNT@", str(frames * channels)).replace(
        "@EXPECTED@", ",".join(map(str, expected))))
    manifests = [source, capture, *[ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                                   for name in ("n_way_mux", "wav_out")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", sample_rate=rate, block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.mux_source"),
        "mux": Node("mux", "orpheus.builtin.n_way_mux", params={
            "inputs": inputs, "channels": channels, "ramp_ms": ramp_ms,
        }),
        "capture": Node("capture", "orpheus.test.mux_capture"),
        "sink": Node("sink", "orpheus.builtin.wav_out", params={
            "channels": channels, "sample_rate": 0, "file_path": str(tmp_path / "output.wav"),
        }),
    }, connections=[
        *[Connection(PortRef("source", f"out{i}"), PortRef("mux", f"in{i}")) for i in (0, inputs - 1)],
        Connection(PortRef("mux", "out"), PortRef("capture", "in")),
        Connection(PortRef("mux", "out"), PortRef("sink", "in")),
    ]))
    update = r'''
        OrpheusValue request={ORPHEUS_VALUE_INT};
        request.value.i32=tick==0 ? INT32_MAX : INT32_MIN;
        const char* key=tick==0 ? "select" : "select_index";
        OrpheusValue actual;
#ifdef DYNAMIC
        if(runtime.set_parameter("mux",key,request) ||
            runtime.get_parameter("mux","select",&actual)) return 5;
#else
        if(orpheus_control_set_value("mux",key,request) ||
            orpheus_control_get_value("mux","select",&actual)) return 5;
#endif
        if(actual.type!=ORPHEUS_VALUE_INT || actual.value.i32!=(tick==0 ? @INPUTS@ : 1)) return 6;
    '''
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})").replace(
        "for(int tick=0;tick<2;tick++) {",
        "for(int tick=0;tick<2;tick++) {" + update.replace("@INPUTS@", str(inputs)))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    plan = case[2]
    assert len(plan.call_graph["source"]) == 2
    assert {edge["input_port"] for edge in plan.call_graph["source"]} == {0, inputs - 1}
    baseline = None
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            result, rows = run_backend(case, backend, mode=mode)
            assert result.returncode == 0, (backend, mode, result.stdout, result.stderr)
            assert rows == [[0, 4, 0, 0], [1, 8, 0, frames]]
            data = (tmp_path / "output.wav").read_bytes()
            if baseline is None:
                baseline = data
            assert data == baseline
            with wave.open(str(tmp_path / "output.wav"), "rb") as stream:
                assert stream.getnchannels() == channels and stream.getnframes() == 8 * frames
                actual = struct.unpack(f"<{len(pcm)}h", stream.readframes(8 * frames))
            assert max(abs(a - b) for a, b in zip(actual, pcm)) <= 1
