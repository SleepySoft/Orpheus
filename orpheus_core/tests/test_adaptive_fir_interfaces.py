"""FxLMS核心：原始参考读出、filtered-x更新、内部目标误差及非实时JSON读回。"""
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
int orpheus_test_af_source_algorithm(orpheus_test_af_source_State* s,
    orpheus_test_af_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
            unsigned index=s->calls*@FRAMES@+n;
            io->x.samples[n*@CHANNELS@+c]=(float)((int)((index*17+c*31)%103)-51)*.03125f;
            io->deriv.samples[n*@CHANNELS@+c]=(float)((int)((index*11+c*19)%61)-30)*.015625f;
            io->error.samples[n*@CHANNELS@+c]=(float)((int)((index*13+c*7)%29)-14)*.0625f;
        }
        io->deriv.buffer->frame_count=ctx->frame_count;
        io->error.buffer->frame_count=ctx->frame_count;
        int rc=orpheus_test_af_source_emit_deriv(ctx,&io->deriv);
        if(rc) return rc;
        rc=orpheus_test_af_source_emit_error(ctx,&io->error);
        if(rc) return rc;
        rc=orpheus_test_af_source_emit_x(ctx,&io->x);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''

ACCESS = r'''
#ifdef DYNAMIC
#define METRIC(v) runtime.get_parameter("afir","conv_metric",v)
#define DETAIL(v) runtime.get_parameter("afir","detail",v)
#define DETAIL_ID(v) runtime.read_id(@ID@,v)
#else
#define METRIC(v) orpheus_control_get_value("afir","conv_metric",v)
#define DETAIL(v) orpheus_control_get_value("afir","detail",v)
#define DETAIL_ID(v) orpheus_control_get_value_id(@ID@,v)
#endif
'''

CHECK = r'''
        const uint32_t expected_metric[]={@METRICS@};
        const char* expected_detail[]={@DETAILS@};
        OrpheusValue value={ORPHEUS_VALUE_STRING,{0}};
        uint32_t metric;
        if(METRIC(&value)) return 8;
        memcpy(&metric,&value.value.f32,sizeof(metric));
        if(value.type!=ORPHEUS_VALUE_FLOAT || metric!=expected_metric[tick]) return 9;
        if(DETAIL(&value) || strcmp(value.value.str,expected_detail[tick])) return 10;
        if(DETAIL_ID(&value) || strcmp(value.value.str,expected_detail[tick])) return 11;
#ifndef DYNAMIC
        unsigned seen=0;
        for(size_t i=0;i<orpheus_control_probe_count();i++) {
            const char* node=NULL;const char* key=NULL;
            if(orpheus_control_probe_get(i,&node,&key,&value)) return 12;
            if(!strcmp(node,"afir") && !strcmp(key,"detail")) {
                if(strcmp(value.value.str,expected_detail[tick])) return 13;
                seen++;
            }
        }
        if(seen!=1) return 14;
#endif
'''


@pytest.mark.parametrize("channels,frames,length,mu,leak,eps,sg", [
    (1, 1, 1, .5, .75, .125, 1.5),
    (3, 7, 5, .125, 1, .03125, 2),
    (32, 1, 1024, .25, .5, 1e-12, 0),
    (32, 3, 17, 2, 0, 1, 10),
])
def test_adaptive_fir_actual_x_arrivals_and_derived_detail(tmp_path, channels, frames, length, mu, leak, eps, sg):
    mu, leak, eps, sg = map(f32, (mu, leak, eps, sg))
    weights = [[0.0] * length for _ in range(channels)]
    expected, metrics = [], []
    for call in range(4):
        output = [0.0] * (frames * channels)
        energy = 0.0
        for channel in range(channels):
            for offset in range(frames):
                n = call * frames + offset
                x = [0.0 if n < k else (((n - k) * 17 + channel * 31) % 103 - 51) * .03125
                     for k in range(length)]
                deriv = [0.0 if n < k else (((n - k) * 11 + channel * 19) % 61 - 30) * .015625
                         for k in range(length)]
                y = norm = 0.0
                for w, raw, filtered in zip(weights[channel], x, deriv):
                    y = f32(y + f32(w * raw))
                    norm = f32(norm + f32(filtered * filtered))
                error = f32(((n * 13 + channel * 7) % 29 - 14) * .0625 - f32(sg * y))
                scale = f32(f32(mu * error) / f32(norm + eps))
                weights[channel] = [f32(f32(leak * w) + f32(scale * filtered))
                                    for w, filtered in zip(weights[channel], deriv)]
                output[offset * channels + channel] = y
                energy += f32(error * error)
        expected.extend(output)
        metrics.append(f32(math.sqrt(energy / (frames * channels + 1e-12))))
    source = create_component(tmp_path, "af_source", {
        "x": audio("output", channels=channels, drive_port_no=1),
        "deriv": audio("output", channels=channels, drive_port_no=2),
        "error": audio("output", channels=channels, drive_port_no=3), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    afir = ROOT / "components" / "orpheus" / "builtin" / "adaptive_fir" / "component.yaml"
    manifests = [source, afir, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.af_source"),
        "afir": Node("afir", "orpheus.builtin.adaptive_fir", params={
            "channels": channels, "filter_length": length, "step_size": mu, "leakage": leak,
            "eps": eps, "secondary_gain": sg}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[
        Connection(PortRef("source", "x"), PortRef("afir", "x")),
        Connection(PortRef("source", "deriv"), PortRef("afir", "deriv")),
        Connection(PortRef("source", "error"), PortRef("afir", "err")),
        Connection(PortRef("afir", "out"), PortRef("capture", "in")),
    ]))
    plan = GraphCompiler(registry).compile(project)
    point = next(point for point in plan.id_map if point["node"] == "afir" and point["key"] == "detail")
    details = [json.dumps(f'{{"filter_length":{length},"conv_metric":{metric:.6g}}}')
               for metric in metrics[1::2]]
    check = CHECK.replace("@METRICS@", ",".join(str(bits(v)) for v in metrics[1::2]))
    check = check.replace("@DETAILS@", ",".join(details))
    harness = "#include <string.h>\n" + four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("int main(", ACCESS.replace("@ID@", str(point["id"])) + "\nint main(")
    harness = harness.replace('        printf("%d', check + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert [(edge["source_interface"], edge["target_interface"])
            for edge in case[2].call_graph["source"]] == [("x", "x")]
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
