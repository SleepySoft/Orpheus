"""真实 HC 交付逐位解包，验证双输入互斥及独立成功标记出口。"""

import pytest

from frequency_interface_fixtures import BIT_FILL, parameter, spectral_bits
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_hc_source_algorithm(orpheus_test_hc_source_State* s,
    orpheus_test_hc_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        int32_t call=s->calls++;
        for(int32_t i=0;i<s->channels*s->fft_size*s->batches;i++) {
            @FILL@
        }
        int rc=orpheus_test_hc_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_hc_capture_algorithm(orpheus_test_hc_capture_State* s,
    orpheus_test_hc_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected[4][@COUNT@]={@EXPECTED@};
    unsigned port=ctx->trigger_input;
    if(s->calls>=8 || port!=(unsigned)(s->calls%2) || ctx->frame_count!=8 || ctx->valid_frames!=8
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/4*8)) return ORPHEUS_ERR_INVALID_ARG;
    if(port) {
        if(io->tick.samples[0]!=1) return ORPHEUS_ERR_PROCESSING;
    } else for(unsigned i=0;i<@COUNT@;i++) {
        uint32_t actual;
        memcpy(&actual,io->in.samples+i,4);
        if(actual!=expected[s->calls/2][i]) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def source_manifest(tmp_path, params, typed, *, both=False):
    port = {"direction": "output", "type": "bulk" if typed else "audio", "sample_format": "f32",
            "channels": "param:channels", "block_size": "param:fft_size*param:batches", "drive_port_no": 1}
    if typed:
        port.update(layout="halfcomplex", frame_size="param:fft_size")
    interfaces = {
        "out": port, "channels": parameter(params["channels"], 64),
        "fft_size": {**parameter(params["fft_size"], 65536), "range": [2, 65536]},
        "batches": parameter(params["batches"], 64), "calls": scalar("int", 0),
    }
    if both:
        interfaces["other"] = {**port, "type": "bulk", "layout": "halfcomplex",
                               "frame_size": "param:fft_size", "drive_port_no": 2}
    return create_component(tmp_path, "hc_source", interfaces, SOURCE.replace("@FILL@", BIT_FILL), source=True)


def verify_halfcomplex_decode(tmp_path, cases, batches):
    for name, config in cases.items():
        typed = config.get("typed", False)
        params = {key: config[key] for key in ("channels", "fft_size", "first_bin", "bins")}
        params["batches"] = batches
        channels, fft_size, first, bins = (params[key] for key in ("channels", "fft_size", "first_bin", "bins"))
        directory = tmp_path / name
        source = source_manifest(directory, params, typed)
        expected = []
        for call in range(4):
            row = []
            for h in range(batches):
                for k in range(first, first + bins):
                    for part in range(2):
                        for c in range(channels):
                            index = (h * fft_size + (k if part == 0 else fft_size - k)) * channels + c
                            row.append(0 if part and k in (0, fft_size // 2) else spectral_bits(index, call))
            expected.append(row)
        capture = create_component(directory, "hc_capture", {
            "in": {"direction": "input", "type": "bulk", "sample_format": "f32",
                   "channels": channels, "block_size": bins * 2 * batches, "process": True},
            "tick": {"direction": "input", "type": "bulk", "sample_format": "i32",
                     "channels": 1, "block_size": 1, "process": True},
            "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
        }, CAPTURE.replace("@COUNT@", str(channels * bins * 2 * batches)).replace(
            "@EXPECTED@", ",".join("{" + ",".join(map(str, row)) + "}" for row in expected)))
        manifest = ROOT / "components" / "orpheus" / "builtin" / "halfcomplex_decode" / "component.yaml"
        manifests = [source, manifest, capture]
        registry = Registry()
        for item in manifests:
            registry.add_manifest(item)
        project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
            "source": Node("source", "orpheus.test.hc_source"),
            "decoder": Node("decoder", "orpheus.builtin.halfcomplex_decode", params=params),
            "capture": Node("capture", "orpheus.test.hc_capture"),
        }, connections=[
            Connection(PortRef("source", "out"), PortRef("decoder", "spectrum" if typed else "in")),
            Connection(PortRef("decoder", "out"), PortRef("capture", "in")),
            Connection(PortRef("decoder", "tick"), PortRef("capture", "tick")),
        ]))
        case = compile_delivery_backends(directory, registry, project, manifests, four_entry_harness())
        assert all(config["frames"] == config["block_size"] == 8 for config in case[2].node_configs.values())
        check_four_entries(case, [[0, 4, 0, 0], [1, 8, 0, 8]])


@pytest.mark.parametrize("typed,channels,fft_size,batches,first,bins", [
    (False, 2, 256, 1, 0, 129), (True, 2, 256, 3, 0, 129),
    (False, 3, 16, 2, 2, 3), (True, 2, 256, 3, 5, 30),
    (False, 64, 2, 64, 0, 2), (True, 3, 6, 2, 0, 4),
])
def test_halfcomplex_decode_four_entries(tmp_path, typed, channels, fft_size, batches, first, bins):
    verify_halfcomplex_decode(tmp_path, {"decode": {
        "channels": channels, "fft_size": fft_size, "first_bin": first, "bins": bins, "typed": typed,
    }}, batches)


def test_halfcomplex_decode_requires_exactly_one_input(tmp_path):
    params = {"channels": 2, "fft_size": 256, "batches": 1}
    audio = source_manifest(tmp_path, params, False, both=True)
    registry = Registry()
    registry.add_manifest(audio)
    registry.add_manifest(ROOT / "components" / "orpheus" / "builtin" / "halfcomplex_decode" / "component.yaml")
    project = Project(graph=Graph(nodes={
        "source": Node("source", "orpheus.test.hc_source"),
        "decoder": Node("decoder", "orpheus.builtin.halfcomplex_decode"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("decoder", "in")),
        Connection(PortRef("source", "other"), PortRef("decoder", "spectrum")),
    ]))
    with pytest.raises(CompileError, match="输入互斥"):
        GraphCompiler(registry).compile(project)
