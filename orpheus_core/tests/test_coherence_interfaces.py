"""冲激频谱的独立EMA预期；显式FFT载荷不改变Task时间线。"""
import json

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_cm_source_algorithm(orpheus_test_cm_source_State* s,
    orpheus_test_cm_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<@FRAMES@;n++) for(unsigned c=0;c<@CHANNELS@;c++)
            io->out.samples[n*@CHANNELS@+c]=n ? 0.0f : (float)((int)((s->calls*3+c*5)%7)-3)*.125f;
        io->out.buffer->frame_count=@FRAMES@;
        int rc=orpheus_test_cm_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''

ACCESS = r'''
#ifdef DYNAMIC
#define READ(key,v) runtime.get_parameter("cm",key,v)
#define READ_ID(id,v) runtime.read_id(id,v)
#else
#define READ(key,v) orpheus_control_get_value("cm",key,v)
#define READ_ID(id,v) orpheus_control_get_value_id(id,v)
#endif
'''

CHECK = r'''
        const char* matrices[]={@MATRIX@};
        const char* histories[]={@HISTORY@};
        const char* keys[]={"coherence","history"};
        const uint32_t ids[]={@IDS@};
        const char* expected[]={matrices[tick],histories[tick]};
        OrpheusValue value={ORPHEUS_VALUE_STRING,{0}};
        for(unsigned i=0;i<2;i++) {
            if(READ(keys[i],&value) || strcmp(value.value.str,expected[i])) return 8;
            if(READ_ID(ids[i],&value) || strcmp(value.value.str,expected[i])) return 9;
        }
#ifndef DYNAMIC
        unsigned seen=0;
        for(size_t i=0;i<orpheus_control_probe_count();i++) {
            const char* node=NULL;const char* key=NULL;
            if(orpheus_control_probe_get(i,&node,&key,&value)) return 10;
            if(strcmp(node,"cm")) continue;
            for(unsigned j=0;j<2;j++) if(!strcmp(key,keys[j])) {
                if(strcmp(value.value.str,expected[j])) return 11;
                seen++;
            }
        }
        if(seen!=2) return 12;
#endif
'''


@pytest.mark.parametrize("channels,frames,smoothing", [(2, 4, 1), (3, 8, 8), (10, 64, 2), (10, 4096, 128)])
def test_coherence_explicit_payload_and_each_real_delivery(tmp_path, channels, frames, smoothing):
    alpha = f32(1 / smoothing)
    power = [0.0] * channels
    cross = [[0.0] * channels for _ in range(channels)]
    expected, matrices, histories, history = [], [], [], []
    for call in range(4):
        amplitudes = [((call * 3 + c * 5) % 7 - 3) * .125 for c in range(channels)]
        expected.extend(amplitudes + [0.0] * ((frames - 1) * channels))
        for i in range(channels):
            power[i] = f32(power[i] + f32(alpha * f32(f32(amplitudes[i] ** 2) - power[i])))
            for j in range(channels):
                instant = f32(amplitudes[i] * amplitudes[j])
                cross[i][j] = f32(cross[i][j] + f32(alpha * f32(instant - cross[i][j])))
        matrix, mean = [], 0.0
        for i in range(channels):
            for j in range(channels):
                denom = f32(power[i] * power[j])
                coherence = min(1.0, f32(f32(cross[i][j] ** 2) / denom)) if denom > f32(1e-12) else 0.0
                matrix.append(coherence)
                if i != j:
                    mean = f32(mean + coherence)
        history.append(f32(mean / (channels * (channels - 1))))
        matrices.append(json.dumps(f'{{"n":{channels},"bins":{frames // 2},"matrix":['
                                   + ",".join(f"{v:.4g}" for v in matrix) + "]}"))
        histories.append(json.dumps("[" + ",".join(f"{v:.4g}" for v in history) + "]"))
    source = create_component(tmp_path, "cm_source", {
        "out": audio("output", channels=channels, block_size=frames, drive_port_no=1),
        "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, block_size=frames, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", "8").replace("@COUNT@", str(channels * frames))
        .replace("if(ctx->frame_count", f"if(io->in.buffer->frame_count!={frames} || ctx->frame_count"))
    cm = ROOT / "components" / "orpheus" / "builtin" / "coherence_matrix" / "component.yaml"
    manifests = [source, cm, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.cm_source"),
        "cm": Node("cm", "orpheus.builtin.coherence_matrix",
                   params={"channels": channels, "fft_size": frames, "smoothing": smoothing}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("cm", "in")),
        Connection(PortRef("cm", "out"), PortRef("capture", "in")),
    ]))
    plan = GraphCompiler(registry).compile(project)
    ids = {point["key"]: point["id"] for point in plan.id_map if point["node"] == "cm"}
    check = CHECK.replace("@MATRIX@", ",".join(matrices[1::2])).replace("@HISTORY@", ",".join(histories[1::2]))
    check = check.replace("@IDS@", f'{ids["coherence"]},{ids["history"]}')
    harness = "#include <string.h>\n" + four_entry_harness()
    harness = harness.replace("int main(", ACCESS + "\nint main(").replace('        printf("%d', check + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0.0, 0], [1, 4, 0.0, 8]])
