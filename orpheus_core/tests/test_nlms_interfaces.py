"""外部误差NLMS的独立f32递推与每次真实参考交付四入口验证。"""
import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_nlms_source_algorithm(orpheus_test_nlms_source_State* s,
    orpheus_test_nlms_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
            unsigned index=s->calls*@FRAMES@+n;
            io->ref.samples[n*@CHANNELS@+c]=(float)((int)((index*17+c*31)%103)-51)*.03125f;
            if(io->error.buffer)
                io->error.samples[n*@CHANNELS@+c]=(float)((int)((index*13+c*7)%29)-14)*.0625f;
        }
        if(io->error.buffer) {
            io->error.buffer->frame_count=ctx->frame_count;
            int rc=orpheus_test_nlms_source_emit_error(ctx,&io->error);
            if(rc) return rc;
        }
        int rc=orpheus_test_nlms_source_emit_ref(ctx,&io->ref);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,length,mu,leak,eps,has_error", [
    (1, 1, 1, .5, .75, .125, True), (3, 7, 5, .125, 1, .03125, True),
    (64, 1, 4096, .25, .5, 1e-12, True), (64, 3, 17, 2, 0, 1, True),
    (3, 8, 3, 1, 0, .125, False),
])
def test_nlms_actual_reference_arrivals_use_external_error(tmp_path, channels, frames, length, mu, leak, eps, has_error):
    mu, leak, eps = f32(mu), f32(leak), f32(eps)
    weights = [[0.0] * length for _ in range(channels)]
    expected = []
    for n in range(4 * frames):
        for channel in range(channels):
            x = [0.0 if n < k else (((n - k) * 17 + channel * 31) % 103 - 51) * .03125
                 for k in range(length)]
            y = norm = 0.0
            for w, value in zip(weights[channel], x):
                y = f32(y + f32(w * value))
                norm = f32(norm + f32(value * value))
            expected.append(y)
            if has_error:
                error = ((n * 13 + channel * 7) % 29 - 14) * .0625
                scale = f32(f32(mu * error) / f32(norm + eps))
                weights[channel] = [f32(f32(leak * w) + f32(scale * value))
                                    for w, value in zip(weights[channel], x)]
    source = create_component(tmp_path, "nlms_source", {
        "ref": audio("output", channels=channels, drive_port_no=1),
        "error": audio("output", channels=channels, drive_port_no=2), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)), source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    nlms = ROOT / "components" / "orpheus" / "builtin" / "nlms" / "component.yaml"
    manifests = [source, nlms, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    edges = [Connection(PortRef("source", "ref"), PortRef("nlms", "ref")),
             Connection(PortRef("nlms", "out"), PortRef("capture", "in"))]
    if has_error:
        edges.append(Connection(PortRef("source", "error"), PortRef("nlms", "err")))
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.nlms_source"),
        "nlms": Node("nlms", "orpheus.builtin.nlms", params={
            "channels": channels, "filter_length": length, "step_size": mu, "leakage": leak, "eps": eps}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=edges))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert [(edge["source_interface"], edge["target_interface"])
            for edge in case[2].call_graph["source"]] == [("ref", "ref")]
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
