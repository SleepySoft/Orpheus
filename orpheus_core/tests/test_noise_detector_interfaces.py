"""显式窗口/批次、解析冲激谱与独立削波/同通道突刺统计。"""
import math

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
int orpheus_test_nd_source_algorithm(orpheus_test_nd_source_State* s,
    orpheus_test_nd_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<@FRAMES@;n++) for(unsigned c=0;c<@CHANNELS@;c++)
            io->out.samples[n*@CHANNELS@+c]=n%@FFT@==@FFT@/2
                ? (float)((int)((s->calls*3+c*5)%9)-4)*.25f : 0.0f;
        io->out.buffer->frame_count=@FRAMES@;
        int rc=orpheus_test_nd_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''

ACCESS = r'''
#ifdef DYNAMIC
#define READ(key,v) runtime.get_parameter("nd",key,v)
#define DETAIL_ID(v) runtime.read_id(@ID@,v)
#else
#define READ(key,v) orpheus_control_get_value("nd",key,v)
#define DETAIL_ID(v) orpheus_control_get_value_id(@ID@,v)
#endif
'''

CHECK = r'''
        const float expected[2][3]={@VALUES@};
        const int expected_clicks[]={@CLICKS@};
        const uint32_t expected_clip_bits[]={@CLIP_BITS@};
        const char* keys[]={"flatness","noise_floor_db","clip_pct"};
        const float tolerance[]={1e-5f,1e-4f,1e-7f};
        OrpheusValue value={ORPHEUS_VALUE_STRING,{0}};
        for(unsigned i=0;i<3;i++) {
            if(READ(keys[i],&value) || value.type!=ORPHEUS_VALUE_FLOAT ||
                !isfinite(value.value.f32) || fabsf(value.value.f32-expected[tick][i])>tolerance[i]) return 8;
            if(i==2) {
                uint32_t bits;memcpy(&bits,&value.value.f32,sizeof(bits));
                if(bits!=expected_clip_bits[tick]) return 9;
            }
        }
        if(READ("clicks",&value) || value.type!=ORPHEUS_VALUE_INT ||
            value.value.i32!=expected_clicks[tick]) return 10;
        if(READ("detail",&value) || value.type!=ORPHEUS_VALUE_STRING) return 11;
        char baseline[1024];strcpy(baseline,value.value.str);
        double flat,noise,ratio;unsigned long long clicks;int used=0;
        if(sscanf(baseline,"{\"flatness\":%lf,\"noise_floor_db\":%lf,\"clicks\":%llu,\"clip_pct\":%lf}%n",
            &flat,&noise,&clicks,&ratio,&used)!=4 || used!=(int)strlen(baseline) ||
            fabs(flat-expected[tick][0])>0.00051 || fabs(noise-expected[tick][1])>0.0051 ||
            fabs(ratio-expected[tick][2])>0.000051 || clicks!=(unsigned)expected_clicks[tick]) return 12;
        if(DETAIL_ID(&value) || strcmp(value.value.str,baseline)) return 13;
#ifndef DYNAMIC
        unsigned seen=0;
        for(size_t i=0;i<orpheus_control_probe_count();i++) {
            const char* node=NULL;const char* key=NULL;
            if(orpheus_control_probe_get(i,&node,&key,&value)) return 14;
            if(!strcmp(node,"nd") && !strcmp(key,"detail")) {
                if(strcmp(value.value.str,baseline)) return 15;
                seen++;
            }
        }
        if(seen!=1) return 16;
#endif
'''


@pytest.mark.parametrize("channels,fft_size,batches,clip,click", [
    (1, 4, 1, .5, .5), (3, 8, 2, .75, .3), (32, 4, 64, 0, 0), (1, 4096, 1, 1, 1),
])
def test_noise_detector_each_explicit_payload_and_latest_probes(tmp_path, channels, fft_size, batches, clip, click):
    clip, click = map(f32, (clip, click))
    frames = fft_size * batches
    expected, probes, counts = [], [], []
    ema = 0.0
    total_samples = total_clipped = total_clicks = 0
    for call in range(4):
        amplitudes = [((call * 3 + c * 5) % 9 - 4) * .25 for c in range(channels)]
        samples = [amplitudes[c] if n % fft_size == fft_size // 2 else 0.0
                   for n in range(frames) for c in range(channels)]
        expected.extend(samples)
        flat = sum(a != 0 for a in amplitudes) / channels
        ema = flat if ema == 0 else ema * .95 + flat * .05
        total_samples += len(samples)
        total_clipped += sum(abs(v) >= clip for v in samples)
        total_clicks += sum(abs(samples[n * channels + c] - samples[(n - 1) * channels + c]) > click
                            for n in range(1, frames) for c in range(channels))
        mean = sum(v * v for v in samples) / (len(samples) + 1.0)
        rmsdb = f32(10 * f32(math.log10(f32(f32(mean) + f32(1e-12)))))
        peakdb = f32(20 * f32(math.log10(max(max(abs(v) for v in samples), f32(1e-9)))))
        floor = rmsdb if ema > .5 else (f32(peakdb - 10) if peakdb > rmsdb else rmsdb)
        probes.append((f32(ema), floor, f32(total_clipped / total_samples)))
        counts.append(total_clicks)
    source = create_component(tmp_path, "nd_source", {
        "out": audio("output", channels=channels, block_size=frames, drive_port_no=1),
        "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)).replace("@FFT@", str(fft_size)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, block_size=frames, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", "8").replace("@COUNT@", str(channels * frames))
        .replace("if(ctx->frame_count", f"if(io->in.buffer->frame_count!={frames} || ctx->frame_count"))
    nd = ROOT / "components" / "orpheus" / "builtin" / "noise_detector" / "component.yaml"
    manifests = [source, nd, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.nd_source"),
        "nd": Node("nd", "orpheus.builtin.noise_detector", params={"channels": channels,
                   "fft_size": fft_size, "batches": batches, "clip_level": clip, "click_thres": click}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("nd", "in")),
        Connection(PortRef("nd", "out"), PortRef("capture", "in")),
    ]))
    plan = GraphCompiler(registry).compile(project)
    point = next(point for point in plan.id_map if point["node"] == "nd" and point["key"] == "detail")
    values = ",".join("{" + ",".join(repr(float(v)) + "f" for v in row) + "}" for row in probes[1::2])
    check = CHECK.replace("@VALUES@", values).replace("@CLICKS@", ",".join(str(v) for v in counts[1::2]))
    check = check.replace("@CLIP_BITS@", ",".join(str(bits(row[2])) for row in probes[1::2]))
    harness = "#include <string.h>\n#include <math.h>\n" + four_entry_harness()
    harness = harness.replace("int main(", ACCESS.replace("@ID@", str(point["id"])) + "\nint main(")
    harness = harness.replace('        printf("%d', check + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0.0, 0], [1, 4, 0.0, 8]])
