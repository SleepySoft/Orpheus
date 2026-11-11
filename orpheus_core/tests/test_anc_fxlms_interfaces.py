"""整体FxLMS的独立非环形参考、累计功率和组件自主交付。"""
import json
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
int orpheus_test_anc_source_algorithm(orpheus_test_anc_source_State* s,
    orpheus_test_anc_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
            unsigned index=s->calls*@FRAMES@+n;
            io->x.samples[n*@CHANNELS@+c]=(float)((int)((index*17+c*31)%103)-51)*.03125f;
            io->d.samples[n*@CHANNELS@+c]=(float)((int)((index*13+c*7)%29)-14)*.0625f;
        }
        io->d.buffer->frame_count=ctx->frame_count;
        int rc=orpheus_test_anc_source_emit_d(ctx,&io->d);
        if(rc) return rc;
        rc=orpheus_test_anc_source_emit_x(ctx,&io->x);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''

ACCESS = r'''
#ifdef DYNAMIC
#define READ(key,v) runtime.get_parameter("anc",key,v)
#define DETAIL_ID(v) runtime.read_id(@ID@,v)
#else
#define READ(key,v) orpheus_control_get_value("anc",key,v)
#define DETAIL_ID(v) orpheus_control_get_value_id(@ID@,v)
#endif
'''

CHECK = r'''
        const uint32_t expected_power[2][2]={@POWERS@};
        const float expected_nr[]={@NR@};
        const char* expected_detail[]={@DETAILS@};
        const char* keys[]={"power_d","power_e"};
        OrpheusValue value={ORPHEUS_VALUE_STRING,{0}};
        for(unsigned i=0;i<2;i++) {
            uint32_t power;
            if(READ(keys[i],&value)) return 8;
            memcpy(&power,&value.value.f32,sizeof(power));
            if(value.type!=ORPHEUS_VALUE_FLOAT || power!=expected_power[tick][i]) return 9;
        }
        if(READ("noise_reduction_db",&value) || !isfinite(value.value.f32) ||
            fabsf(value.value.f32-expected_nr[tick])>2e-5f) return 10;
        if(READ("detail",&value) || strcmp(value.value.str,expected_detail[tick])) return 11;
        if(DETAIL_ID(&value) || strcmp(value.value.str,expected_detail[tick])) return 12;
#ifndef DYNAMIC
        unsigned seen=0;
        for(size_t i=0;i<orpheus_control_probe_count();i++) {
            const char* node=NULL;const char* key=NULL;
            if(orpheus_control_probe_get(i,&node,&key,&value)) return 13;
            if(!strcmp(node,"anc") && !strcmp(key,"detail")) {
                if(strcmp(value.value.str,expected_detail[tick])) return 14;
                seen++;
            }
        }
        if(seen!=1) return 15;
#endif
'''


@pytest.mark.parametrize("channels,frames,length,mu,leak,eps,sg,delay", [
    (1, 1, 1, .5, .75, .125, 1.5, 0),
    (3, 7, 5, .125, 1, .03125, 2, 1.75),
    (8, 1, 1024, .25, .5, 1e-12, .5, 512),
    (8, 3, 17, 0, 0, 1, 0, 512),
])
def test_anc_fxlms_actual_arrivals_and_cumulative_power(tmp_path, channels, frames, length, mu, leak, eps, sg, delay):
    mu, leak, eps, sg, delay = map(f32, (mu, leak, eps, sg, delay))
    weights = [[0.0] * length for _ in range(channels)]
    expected, probes, details = [], [], []
    accumulated_d = accumulated_e = 0.0
    for call in range(4):
        output = [0.0] * (frames * channels)
        pd = pe = 0.0
        for channel in range(channels):
            for offset in range(frames):
                n = call * frames + offset
                history = [0.0 if n < k else (((n - k) * 17 + channel * 31) % 103 - 51) * .03125
                           for k in range(length)]
                filtered = [f32(sg * history[(k + int(delay)) % length]) for k in range(length)]
                norm = y = 0.0
                for value in filtered:
                    norm = f32(norm + f32(value * value))
                for w, raw in zip(weights[channel], history):
                    y = f32(y + f32(w * raw))
                d = ((n * 13 + channel * 7) % 29 - 14) * .0625
                error = f32(d - f32(sg * y))
                scale = f32(f32(mu * error) / f32(norm + eps))
                weights[channel] = [f32(f32(leak * w) + f32(scale * value))
                                    for w, value in zip(weights[channel], filtered)]
                output[offset * channels + channel] = -y
                pd += f32(d * d)
                pe += f32(error * error)
        expected.extend(output)
        accumulated_d += pd
        accumulated_e += pe
        power_d = f32(accumulated_d / ((call + 1) * frames * channels))
        power_e = f32(accumulated_e / ((call + 1) * frames * channels))
        nr = f32(10 * f32(math.log10(f32(power_d / f32(power_e + f32(1e-30)))))) if power_e > f32(1e-30) else 0
        probes.append((power_d, power_e, nr))
        details.append(json.dumps(f'{{"noise_reduction_db":{nr:.2f},"power_d":{power_d:.6g},'
                                  f'"power_e":{power_e:.6g},"frames":{call + 1}}}'))
    source = create_component(tmp_path, "anc_source", {
        "x": audio("output", channels=channels, drive_port_no=1),
        "d": audio("output", channels=channels, drive_port_no=2), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    anc = ROOT / "components" / "orpheus" / "builtin" / "anc_fxlms" / "component.yaml"
    manifests = [source, anc, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.anc_source"),
        "anc": Node("anc", "orpheus.builtin.anc_fxlms", params={
            "channels": channels, "filter_length": length, "step_size": mu, "leakage": leak,
            "eps": eps, "secondary_gain": sg, "secondary_delay": delay}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "x"), PortRef("anc", "x")),
        Connection(PortRef("source", "d"), PortRef("anc", "d")),
        Connection(PortRef("anc", "out"), PortRef("capture", "in")),
    ]))
    plan = GraphCompiler(registry).compile(project)
    point = next(point for point in plan.id_map if point["node"] == "anc" and point["key"] == "detail")
    powers = ",".join("{" + ",".join(str(bits(v)) for v in row[:2]) + "}" for row in probes[1::2])
    check = CHECK.replace("@POWERS@", powers).replace("@NR@", ",".join(repr(p[2]) + "f" for p in probes[1::2]))
    check = check.replace("@DETAILS@", ",".join(details[1::2]))
    harness = "#include <string.h>\n#include <math.h>\n" + four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("int main(", ACCESS.replace("@ID@", str(point["id"])) + "\nint main(")
    harness = harness.replace('        printf("%d', check + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert [(edge["source_interface"], edge["target_interface"])
            for edge in case[2].call_graph["source"]] == [("x", "x")]
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
