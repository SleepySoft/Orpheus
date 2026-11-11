"""同位冲激的解析互谱、逐窗口计数及ref数据口/被测驱动口四入口验证。"""
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
int orpheus_test_ab_source_algorithm(orpheus_test_ab_source_State* s,
    orpheus_test_ab_source_IO* io,const OrpheusProcessContext* ctx) {
    s->frame=(int32_t)ctx->frame_index;
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<@FRAMES@;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
            bool pulse=n%@FFT@==@FFT@/2;
            io->ref.samples[n*@CHANNELS@+c]=pulse && s->calls%3!=1 ? 1.0f : 0.0f;
            io->in.samples[n*@CHANNELS@+c]=pulse ? ((s->calls+n/@FFT@+c)%2 ? -1.0f : 1.0f) : 0.0f;
        }
        io->ref.buffer->frame_count=@FRAMES@;
        io->in.buffer->frame_count=@FRAMES@;
        int rc=orpheus_test_ab_source_emit_ref(ctx,&io->ref);
        if(rc) return rc;
        rc=orpheus_test_ab_source_emit_in(ctx,&io->in);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''

ACCESS = r'''
#ifdef DYNAMIC
#define READ(key,v) runtime.get_parameter("ab",key,v)
#define DETAIL_ID(v) runtime.read_id(@ID@,v)
#else
#define READ(key,v) orpheus_control_get_value("ab",key,v)
#define DETAIL_ID(v) orpheus_control_get_value_id(@ID@,v)
#endif
'''

CHECK = r'''
        const float expected[2][6]={@VALUES@};
        const unsigned long long counters[2][5]={@COUNTERS@};
        const char* keys[]={"thd_n_db","noise_ratio","residue_pk"};
        const float tolerance[]={.005f,1e-7f,0.0f};
        OrpheusValue value={ORPHEUS_VALUE_STRING,{0}};
        for(unsigned i=0;i<3;i++) {
            if(READ(keys[i],&value) || value.type!=ORPHEUS_VALUE_FLOAT || !isfinite(value.value.f32) ||
                fabsf(value.value.f32-expected[tick][i])>tolerance[i]) return 8;
        }
        if(READ("noise_frames",&value) || value.type!=ORPHEUS_VALUE_INT ||
            value.value.i32!=(int)counters[tick][0]) return 9;
        if(READ("clicks",&value) || value.type!=ORPHEUS_VALUE_INT ||
            value.value.i32!=(int)counters[tick][4]) return 10;
        if(READ("detail",&value) || value.type!=ORPHEUS_VALUE_STRING) return 11;
        char baseline[1024];strcpy(baseline,value.value.str);
        double thd,coh[3],peak;unsigned long long parsed[5];int used=0;
        if(sscanf(baseline,"{\"thd_n_db\":%lf,\"coh_lo\":%lf,\"coh_mid\":%lf,\"coh_hi\":%lf,"
            "\"noise_frames\":%llu,\"total_frames\":%llu,\"noisy_samples\":%llu,\"total_samples\":%llu,"
            "\"clicks\":%llu,\"residue_pk\":%lf}%n",&thd,coh,coh+1,coh+2,parsed,parsed+1,parsed+2,
            parsed+3,parsed+4,&peak,&used)!=10 || used!=(int)strlen(baseline) ||
            fabs(thd-expected[tick][0])>.0101 || fabs(peak-expected[tick][2])>.000051) return 12;
        for(unsigned i=0;i<3;i++) if(fabs(coh[i]-expected[tick][i+3])>.0006) return 13;
        for(unsigned i=0;i<5;i++) if(parsed[i]!=counters[tick][i]) return 14;
        if(DETAIL_ID(&value) || strcmp(value.value.str,baseline)) return 15;
#ifndef DYNAMIC
        unsigned seen=0;
        for(size_t i=0;i<orpheus_control_probe_count();i++) {
            const char* node=NULL;const char* key=NULL;
            if(orpheus_control_probe_get(i,&node,&key,&value)) return 16;
            if(!strcmp(node,"ab") && !strcmp(key,"detail")) {
                if(strcmp(value.value.str,baseline)) return 17;
                seen++;
            }
        }
        if(seen!=1) return 18;
#endif
'''


def _thd(residue, power):
    return f32(10 * f32(math.log10(f32(max(residue / power, 1e-12))))) if power > 1e-12 else -120.0


@pytest.mark.parametrize("channels,fft_size,batches,smoothing,threshold,time,observe_only", [
    (1, 4, 1, 1, -120, 1, False), (3, 8, 2, 8, -40, .02, False),
    (32, 4, 64, 128, -40, 0, False), (1, 4096, 1, 2, 0, 1, False),
    (3, 8, 2, 8, -40, .02, True),
])
def test_noise_detector_ab_reference_is_data_only_and_counts_real_windows(
    tmp_path, channels, fft_size, batches, smoothing, threshold, time, observe_only
):
    frames, half = fft_size * batches, fft_size // 2
    alpha, time = f32(1 / smoothing), f32(time)
    xx, yy, xy = [0.0] * channels, [0.0] * channels, [0.0] * channels
    expected, probes, counts = [], [], []
    noise_windows = total_windows = noisy_samples = clicks = 0
    peak = 0.0
    for call in range(4):
        x = float(call % 3 != 1)
        accumulated_power = accumulated_residue = 0.0
        for window in range(batches):
            signs = [-1.0 if (call + window + c) % 2 else 1.0 for c in range(channels)]
            expected.extend([0.0] * (fft_size // 2 * channels) + signs
                            + [0.0] * ((fft_size - fft_size // 2 - 1) * channels))
            for c, sign in enumerate(signs):
                xx[c] = f32(xx[c] + f32(alpha * f32(x * x - xx[c])))
                yy[c] = f32(yy[c] + f32(alpha * f32(1.0 - yy[c])))
                xy[c] = f32(xy[c] + f32(alpha * f32(x * sign - xy[c])))
                residue = abs(x - sign)
                noisy_samples += residue > time
                clicks += residue > f32(.2)
                peak = max(peak, residue)
            gamma = [min(1.0, f32(f32(z * z) / f32(a * b))) if f32(a * b) > f32(1e-12) else 0
                     for a, b, z in zip(xx, yy, xy)]
            power = sum(v * (half - 1) for v in yy)
            residue = sum(f32(f32(1.0 - g) * v) * (half - 1) for g, v in zip(gamma, yy))
            noise_windows += _thd(residue, power) > threshold
            total_windows += 1
            accumulated_power += power
            accumulated_residue += residue
        low = max(1, half // 4)
        middle = max(low + 1, 3 * half // 4)
        mean = f32(sum(gamma) / channels)
        band_counts = [sum(k < low for k in range(1, half)),
                       sum(low <= k < middle for k in range(1, half)),
                       sum(k >= middle for k in range(1, half))]
        bands = [mean if count else 0.0 for count in band_counts]
        probes.append((_thd(accumulated_residue, accumulated_power),
                       f32(noise_windows / total_windows), peak, *bands))
        counts.append((noise_windows, total_windows, noisy_samples, (call + 1) * frames * channels, clicks))
    source = create_component(tmp_path, "ab_source", {
        "ref": audio("output", channels=channels, block_size=frames, drive_port_no=2),
        "in": audio("output", channels=channels, block_size=frames, drive_port_no=1),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)).replace("@FFT@", str(fft_size)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, block_size=frames, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", "8").replace("@COUNT@", str(channels * frames))
        .replace("if(ctx->frame_count", f"if(io->in.buffer->frame_count!={frames} || ctx->frame_count"))
    ab = ROOT / "components" / "orpheus" / "builtin" / "noise_detector_ab" / "component.yaml"
    manifests = [source, ab, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.ab_source"),
        "ab": Node("ab", "orpheus.builtin.noise_detector_ab", params={"channels": channels,
                   "fft_size": fft_size, "batches": batches, "smoothing": smoothing,
                   "threshold_db": threshold, "time_thres": time}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "ref"), PortRef("ab", "ref")),
        Connection(PortRef("source", "in"), PortRef("ab", "in")),
        Connection(PortRef("ab", "out"), PortRef("capture", "in")),
    ]))
    if observe_only:
        del project.graph.nodes["capture"]
        project.graph.connections.pop()
    plan = GraphCompiler(registry).compile(project)
    point = next(point for point in plan.id_map if point["node"] == "ab" and point["key"] == "detail")
    values = ",".join("{" + ",".join(repr(float(v)) + "f" for v in row) + "}" for row in probes[1::2])
    counters = ",".join("{" + ",".join(str(v) + "ULL" for v in row) + "}" for row in counts[1::2])
    check = CHECK.replace("@VALUES@", values).replace("@COUNTERS@", counters)
    harness = "#include <string.h>\n#include <math.h>\n" + four_entry_harness()
    if observe_only:
        harness = harness.replace('"capture"', '"source"')
    harness = harness.replace("int main(", ACCESS.replace("@ID@", str(point["id"])) + "\nint main(")
    harness = harness.replace('        printf("%d', check + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert [(edge["source_interface"], edge["target_interface"])
            for edge in case[2].call_graph["source"]] == [("in", "in")]
    check_four_entries(case, [[0, 2, 0.0, 0], [1, 4, 0.0, 8]])
