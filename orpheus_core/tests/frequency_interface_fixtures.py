"""频域接口的真实源、逐出口位型采集与四入口执行。"""

import math
import struct

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


PATTERNS = (0, 0x80000000, 0x7F800000, 0xFF800000, 0x7FC12345, 0x7F812345, 1, 0x3F123456)

SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_frequency_source_algorithm(orpheus_test_frequency_source_State* s,
    orpheus_test_frequency_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        int32_t call=s->calls++;
        for(int32_t i=0;i<s->channels*s->elements*2;i++) {
            @FILL@
        }
        if(io->gain.buffer)
            for(int32_t i=0;i<s->channels*s->elements;i++)
                io->gain.samples[i]=((i*17+call*13)%129-64)*.03125f;
        int rc=orpheus_test_frequency_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <math.h>
#include <string.h>
int orpheus_test_frequency_capture_algorithm(orpheus_test_frequency_capture_State* s,
    orpheus_test_frequency_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t expected_a[4][@A_COUNT@]={@A_EXPECTED@};
    static const uint32_t expected_b[4][@B_COUNT@]={@B_EXPECTED@};
    unsigned port=ctx->trigger_input;
    if(s->calls>=8 || port!=(unsigned)(s->calls%2) || ctx->frame_count!=@STEP@
        || ctx->valid_frames!=@STEP@ || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/4*@STEP@)) return ORPHEUS_ERR_INVALID_ARG;
    const float* samples=port ? io->b.samples : io->a.samples;
    const uint32_t* expected=port ? expected_b[s->calls/2] : expected_a[s->calls/2];
    for(unsigned i=0;i<(port ? @B_COUNT@ : @A_COUNT@);i++) {
        uint32_t actual;
        memcpy(&actual,samples+i,4);
        if(@APPROX@ && port) {
            float reference;
            memcpy(&reference,expected+i,4);
            if(!isfinite(samples[i]) || fabsf(samples[i]-reference)>0.00001f)
                return ORPHEUS_ERR_PROCESSING;
        } else if(actual!=expected[i]) return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def source_values(channels, elements, call):
    return [((i * 29 + call * 7) % 257 - 128) * .0078125 for i in range(channels * elements * 2)]


def gain_values(channels, elements, call):
    return [((i * 17 + call * 13) % 129 - 64) * .03125 for i in range(channels * elements)]


def parameter(initial, maximum):
    return {**scalar("int", initial), "range": [1, maximum],
            "affects_signature": True, "update_policy": "restart_required"}


def frequency_graph(tmp_path, component, params, frames, expected, *, step=8, typed=False,
                    patterns=False, gain=False, approximate_energy=False):
    channels = params["channels"]
    elements = params.get("elements", params.get("bins", 1) * params.get("batches", 1))
    bulk = {"type": "bulk", "sample_format": "f32", "channels": "param:channels"}
    fill = "io->out.samples[i]=((i*29+call*7)%257-128)*.0078125f;"
    if patterns:
        fill = ("static const uint32_t patterns[]={" + ",".join(map(str, PATTERNS))
                + "}; uint32_t value=patterns[(i+call)%8]; memcpy(io->out.samples+i,&value,4);")
    source = create_component(tmp_path, "frequency_source", {
        "out": {**bulk, "direction": "output", "block_size": "param:elements*2", "drive_port_no": 1},
        "gain": {**bulk, "direction": "output", "block_size": "param:elements"},
        "channels": parameter(channels, 64), "elements": parameter(elements, 4194304),
        "calls": scalar("int", 0),
    }, SOURCE.replace("@FILL@", fill), source=True)
    capture_ports = {}
    for index, key in enumerate(("a", "b")):
        capture_ports[key] = {**bulk, "direction": "input", "block_size": f"param:frames_{key}",
                              "process": True}
        if typed and key == "a":
            capture_ports[key]["type"] = "audio"
        if typed and key == "b":
            capture_ports[key].update(layout="halfcomplex", frame_size="param:fft_size")
    capture_interfaces = {
        **capture_ports, "channels": parameter(channels, 64),
        "frames_a": parameter(frames[0], 8388608), "frames_b": parameter(frames[1], 8388608),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }
    if typed:
        capture_interfaces["fft_size"] = {**parameter(params["fft_size"], 65536), "range": [2, 65536]}
    code = CAPTURE
    for key, rows in zip(("A", "B"), expected):
        code = code.replace(f"@{key}_COUNT@", str(channels * frames[0 if key == "A" else 1]))
        code = code.replace(f"@{key}_EXPECTED@",
                            ",".join("{" + ",".join(map(str, row)) + "}" for row in rows))
    code = code.replace("@STEP@", str(step)).replace("@APPROX@", str(int(approximate_energy)))
    capture = create_component(tmp_path, "frequency_capture", capture_interfaces, code)
    manifest = ROOT / "components" / "orpheus" / "builtin" / component / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    outputs = {"complex_power": ("power", "energy"),
               "complex_gain_split": ("impulse", "reverb"),
               "halfcomplex_encode": ("out", "spectrum")}[component]
    connections = [Connection(PortRef("source", "out"), PortRef("processor", "in"))]
    if gain:
        connections.append(Connection(PortRef("source", "gain"), PortRef("processor", "gain")))
    connections += [Connection(PortRef("processor", output), PortRef("capture", key))
                    for output, key in zip(outputs, ("a", "b"))]
    capture_params = {"channels": channels, "frames_a": frames[0], "frames_b": frames[1]}
    if typed:
        capture_params["fft_size"] = params["fft_size"]
    project = Project(flow_mode="task_sequence", block_size=step, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.frequency_source",
                       params={"channels": channels, "elements": elements}),
        "processor": Node("processor", f"orpheus.builtin.{component}", params=params),
        "capture": Node("capture", "orpheus.test.frequency_capture", params=capture_params),
    }, connections=connections))
    harness = four_entry_harness().replace(",8)", f",{step})").replace("(8)", f"({step})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    for config in case[2].node_configs.values():
        assert config["frames"] == config["block_size"] == step
    if typed:
        assert case[2].node_configs["capture"]["input_audio_payload_ports"] == ["a"]
    check_four_entries(case, [[0, 4, 0, 0], [1, 8, 0, step]])
    return case


def power_expected(channels, bins, batches, reference=None):
    powers, energies = [], []
    for call in range(4):
        values = source_values(channels, bins * batches, call)
        if reference:
            p, e = reference(struct.pack(f"<{len(values)}f", *values))
            powers.append(list(struct.unpack(f"<{len(p)//4}I", p)))
            energies.append(list(struct.unpack(f"<{len(e)//4}I", e)))
            continue
        p = []
        for point in range(bins * batches):
            for channel in range(channels):
                real = values[point * channels * 2 + channel]
                imaginary = values[point * channels * 2 + channel + channels]
                p.append(f32(f32(real * real) + f32(imaginary * imaginary)))
        e = []
        for batch in range(batches):
            for channel in range(channels):
                total = -0.0
                for k in range(bins):
                    total = f32(total + p[(batch * bins + k) * channels + channel])
                e.append(f32(f32(math.log10(f32(total + f32(2.22044605e-16)))) * 10))
        powers.append([bits(value) for value in p])
        energies.append([bits(value) for value in e])
    return powers, energies
