"""NLMS内部误差的独立非环形f32递推与double能量/调用统计。"""
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
int orpheus_test_nr_source_algorithm(orpheus_test_nr_source_State* s,
    orpheus_test_nr_source_IO* io,const OrpheusProcessContext* ctx) {
    s->frame=(int32_t)ctx->frame_index;
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
            unsigned index=s->calls*@FRAMES@+n;
            io->ref.samples[n*@CHANNELS@+c]=(float)((int)((index*17+c*31)%103)-51)*.03125f;
            io->in.samples[n*@CHANNELS@+c]=(float)((int)((index*13+c*7)%29)-14)*.0625f;
        }
        io->ref.buffer->frame_count=ctx->frame_count;
        int rc=orpheus_test_nr_source_emit_ref(ctx,&io->ref);
        if(rc) return rc;
        rc=orpheus_test_nr_source_emit_in(ctx,&io->in);
        if(rc) return rc;
        s->last=io->in.samples[@FRAMES@*@CHANNELS@-1];s->calls++;
    }
    return ORPHEUS_OK;
}
'''

ACCESS = r'''
#ifdef DYNAMIC
#define READ(key,v) runtime.get_parameter("nr",key,v)
#define DETAIL_ID(v) runtime.read_id(@ID@,v)
#else
#define READ(key,v) orpheus_control_get_value("nr",key,v)
#define DETAIL_ID(v) orpheus_control_get_value_id(@ID@,v)
#endif
'''

CHECK = r'''
        const float expected[2][4]={@VALUES@};
        const unsigned long long counters[2][5]={@COUNTERS@};
        const uint32_t ratio_bits[]={@RATIO@};
        const char* keys[]={"residue_db","erle_db","noise_ratio","residue_pk"};
        const float tolerance[]={1e-4f,1e-4f,1e-7f,0.0f};
        OrpheusValue value={ORPHEUS_VALUE_STRING,{0}};
        for(unsigned i=0;i<4;i++) {
            if(READ(keys[i],&value) || value.type!=ORPHEUS_VALUE_FLOAT || !isfinite(value.value.f32) ||
                fabsf(value.value.f32-expected[tick][i])>tolerance[i]) return 8;
            if(i==2) {
                uint32_t bits;memcpy(&bits,&value.value.f32,sizeof(bits));
                if(bits!=ratio_bits[tick]) return 9;
            }
        }
        if(READ("noise_frames",&value) || value.type!=ORPHEUS_VALUE_INT ||
            value.value.i32!=(int)counters[tick][0]) return 10;
        if(READ("clicks",&value) || value.type!=ORPHEUS_VALUE_INT ||
            value.value.i32!=(int)counters[tick][4]) return 11;
        if(READ("detail",&value) || value.type!=ORPHEUS_VALUE_STRING) return 12;
        char baseline[1024];strcpy(baseline,value.value.str);
        double residue,erle,peak;unsigned long long parsed[5];int used=0;
        if(sscanf(baseline,"{\"residue_db\":%lf,\"erle_db\":%lf,\"noise_frames\":%llu,\"total_frames\":%llu,"
            "\"noisy_samples\":%llu,\"total_samples\":%llu,\"clicks\":%llu,\"residue_pk\":%lf}%n",
            &residue,&erle,parsed,parsed+1,parsed+2,parsed+3,parsed+4,&peak,&used)!=8 ||
            used!=(int)strlen(baseline) || fabs(residue-expected[tick][0])>.0051 ||
            fabs(erle-expected[tick][1])>.0051 || fabs(peak-expected[tick][3])>.000051) return 13;
        for(unsigned i=0;i<5;i++) if(parsed[i]!=counters[tick][i]) return 14;
        if(DETAIL_ID(&value) || strcmp(value.value.str,baseline)) return 15;
#ifndef DYNAMIC
        unsigned seen=0;
        for(size_t i=0;i<orpheus_control_probe_count();i++) {
            const char* node=NULL;const char* key=NULL;
            if(orpheus_control_probe_get(i,&node,&key,&value)) return 16;
            if(!strcmp(node,"nr") && !strcmp(key,"detail")) {
                if(strcmp(value.value.str,baseline)) return 17;
                seen++;
            }
        }
        if(seen!=1) return 18;
#endif
'''


@pytest.mark.parametrize("channels,frames,length,mu,leak,eps,time,threshold,observe_only", [
    (1, 1, 1, .5, .75, .125, .25, -25, False),
    (3, 7, 5, .125, 1, .03125, .02, -40, False),
    (32, 1, 4096, .25, .5, 1e-12, 0, -120, False),
    (32, 3, 17, 1, 0, 1, 1, 0, False),
    (3, 7, 5, .125, 1, .03125, .02, -40, True),
])
def test_noise_detector_nlms_internal_error_and_actual_arrival_statistics(
    tmp_path, channels, frames, length, mu, leak, eps, time, threshold, observe_only
):
    mu, leak, eps, time = map(f32, (mu, leak, eps, time))
    weights = [[0.0] * length for _ in range(channels)]
    expected, probes, counts = [], [], []
    accumulated_input = accumulated_residue = 0.0
    noise_calls = noisy_samples = clicks = 0
    peak = 0.0
    for call in range(4):
        measured = [((n * 13 + c * 7) % 29 - 14) * .0625
                    for n in range(call * frames, (call + 1) * frames) for c in range(channels)]
        expected.extend(measured)
        input_energy = residue_energy = 0.0
        for c in range(channels):
            for offset in range(frames):
                n = call * frames + offset
                history = [0.0 if n < k else (((n - k) * 17 + c * 31) % 103 - 51) * .03125
                           for k in range(length)]
                y = norm = 0.0
                for w, raw in zip(weights[c], history):
                    y = f32(y + f32(w * raw))
                    norm = f32(norm + f32(raw * raw))
                d = measured[offset * channels + c]
                error = f32(d - y)
                residue_energy += error * error
                input_energy += d * d
                noisy_samples += abs(error) > time
                clicks += abs(error) > f32(.2)
                peak = max(peak, abs(error))
                scale = f32(f32(mu * error) / f32(norm + eps))
                weights[c] = [f32(f32(leak * w) + f32(scale * raw)) for w, raw in zip(weights[c], history)]
        db = 10 * math.log10((residue_energy + 1e-12) / (input_energy + 1e-12))
        noise_calls += db > threshold
        accumulated_input += input_energy
        accumulated_residue += residue_energy
        erle = f32(10 * f32(math.log10(f32((accumulated_input + 1e-12) / (accumulated_residue + 1e-12)))))
        probes.append((f32(db), erle, f32(noise_calls / (call + 1)), peak))
        counts.append((noise_calls, call + 1, noisy_samples, (call + 1) * frames * channels, clicks))
    source = create_component(tmp_path, "nr_source", {
        "ref": audio("output", channels=channels, drive_port_no=2),
        "in": audio("output", channels=channels, drive_port_no=1),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    nr = ROOT / "components" / "orpheus" / "builtin" / "noise_detector_nlms" / "component.yaml"
    manifests = [source, nr, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.nr_source"),
        "nr": Node("nr", "orpheus.builtin.noise_detector_nlms", params={"channels": channels,
                   "filter_length": length, "step_size": mu, "leakage": leak, "eps": eps,
                   "time_thres": time, "frame_thres_db": threshold}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "ref"), PortRef("nr", "ref")),
        Connection(PortRef("source", "in"), PortRef("nr", "in")),
        Connection(PortRef("nr", "out"), PortRef("capture", "in")),
    ]))
    if observe_only:
        del project.graph.nodes["capture"]
        project.graph.connections.pop()
    plan = GraphCompiler(registry).compile(project)
    point = next(point for point in plan.id_map if point["node"] == "nr" and point["key"] == "detail")
    values = ",".join("{" + ",".join(repr(float(v)) + "f" for v in row) + "}" for row in probes[1::2])
    counters = ",".join("{" + ",".join(str(v) + "ULL" for v in row) + "}" for row in counts[1::2])
    check = CHECK.replace("@VALUES@", values).replace("@COUNTERS@", counters)
    check = check.replace("@RATIO@", ",".join(str(bits(row[2])) for row in probes[1::2]))
    harness = "#include <string.h>\n#include <math.h>\n" + four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    if observe_only:
        harness = harness.replace('"capture"', '"source"')
    harness = harness.replace("int main(", ACCESS.replace("@ID@", str(point["id"])) + "\nint main(")
    harness = harness.replace('        printf("%d', check + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert [(edge["source_interface"], edge["target_interface"])
            for edge in case[2].call_graph["source"]] == [("in", "in")]
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
