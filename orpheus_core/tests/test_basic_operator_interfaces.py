"""基本运算、逐入口乘法和音频到标量链分别对照独立预期。"""

import math
import struct

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import HARNESS, ROOT, audio, compile_delivery_backends, create_component, scalar


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_operator_source_algorithm(orpheus_test_operator_source_State* s,
    orpheus_test_operator_source_IO* io, const OrpheusProcessContext* ctx) {
    if(ctx->frame_count!=8 || !io->a.buffer) return ORPHEUS_ERR_INVALID_ARG;
    if(s->operation==3) {
        uint32_t bits=(uint32_t)s->bits;
        if(s->mode==5 && s->calls) bits=0x3fe00000;
        memcpy(io->a.samples,&bits,sizeof(bits));
        io->a.buffer->frame_count=1;
    } else {
        for(int32_t i=0;i<s->frames*s->channels;i++)
            io->a.samples[i]=(i%17-4)*0.25f+s->calls*0.5f;
        io->a.buffer->frame_count=(uint32_t)s->frames;
    }
    int result=orpheus_test_operator_source_emit_a(ctx,&io->a);
    if(result!=ORPHEUS_OK) { s->calls++; return result; }
    if(s->operation==1) {
        if(!io->b.buffer) return ORPHEUS_ERR_INVALID_ARG;
        for(int32_t i=0;i<s->frames*s->channels;i++)
            io->b.samples[i]=(float)((i%3+1)*(s->calls+2));
        io->b.buffer->frame_count=(uint32_t)s->frames;
        result=orpheus_test_operator_source_emit_b(ctx,&io->b);
    }
    s->calls++;
    return result;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>
int orpheus_test_operator_capture_algorithm(orpheus_test_operator_capture_State* s,
    orpheus_test_operator_capture_IO* io, const OrpheusProcessContext* ctx) {
    uint32_t port=ctx->trigger_input;
    if(port>=io->in_count || !io->in[port].buffer) return ORPHEUS_ERR_INVALID_ARG;
    int events=s->operation==1 ? 2 : s->operation==2 ? s->source_channels : 1;
    int tick=s->operation==3 ? (int)(ctx->frame_index/8) : s->calls/events;
    uint32_t quantum=s->operation<2 ? (uint32_t)s->frames : 8u;
    if(ctx->frame_count!=quantum || ctx->valid_frames!=quantum
        || ctx->frame_index!=(uint64_t)tick*8 || ctx->epoch!=1)
        return ORPHEUS_ERR_INVALID_ARG;
    if(s->operation==2 && port!=(uint32_t)(s->calls%events))
        return ORPHEUS_ERR_UNSUPPORTED;
    uint32_t count=s->operation<2 ? quantum*(uint32_t)s->channels : 1;
    if(io->in[port].buffer->frame_count!=(s->operation<2 ? quantum : 1))
        return ORPHEUS_ERR_INVALID_ARG;
    for(uint32_t i=0;i<count;i++) {
        uint32_t index=s->operation==2 ? (uint32_t)s->selected_frame*s->source_channels+port : i;
        float expected=((int32_t)(index%17)-4)*0.25f+tick*0.5f;
        if(s->operation==0) expected=-expected;
        if(s->operation==1) {
            int factor=s->calls%2 ? tick+2 : tick ? tick+1 : 0;
            expected*=(float)((i%3+1)*factor);
        }
        if(s->operation==3) {
            uint32_t bits=(uint32_t)s->bits;
            if(s->recovery && tick) bits=0x3fe00000;
            memcpy(&expected,&bits,sizeof(bits));
            if(!isfinite(expected) || (double)expected < -2147483648.0
                || (double)expected >= 2147483648.0) return ORPHEUS_ERR_UNSUPPORTED;
        }
        @COMPARE@
        s->total+=(float)io->in[port].samples[i];
    }
    s->last=(float)io->in[port].samples[0];
    s->trace=(s->trace*33+(int32_t)port+1)%100003;
    s->calls++;
    s->frame=(int32_t)ctx->frame_index;
    s->epoch=(int32_t)ctx->epoch;
    return ORPHEUS_OK;
}
'''

ARENA_CHECK = r'''
    std::vector<OrpheusResolvedData> entries;
    if(runtime.resolve_all(&entries)) return 5;
    OrpheusResolvedData arena={};
    if(runtime.resolve(ORPHEUS_ID_MAKE(ORPHEUS_ID_TUNE,0,ORPHEUS_ID_SLOT_MODULE),&arena))
        return 5;
    if(!arena.base) return 5;
    uintptr_t begin=(uintptr_t)arena.base;
    size_t end=0,alignment=1;
    for(const auto& entry:entries) {
        if(entry.form==ORPHEUS_FORM_MODULE || !entry.node || !entry.node[0]) continue;
        const auto* iface=runtime.get_interface(entry.node);
        if(!iface) return 5;
        const auto* descriptor=iface->get_descriptor();
        uintptr_t state=(uintptr_t)entry.base-entry.offset;
        if(state<begin || state-begin>arena.byte_size
            || descriptor->state_size>arena.byte_size-(state-begin)
            || state%descriptor->alignment) return 5;
        end=std::max(end,(size_t)(state-begin)+descriptor->state_size);
        alignment=std::max(alignment,descriptor->alignment);
    }
    OrpheusValue operation={};
    if(runtime.get_parameter("source","operation",&operation)) return 5;
    if(operation.value.i32==0 && arena.byte_size!=(end+alignment-1)/alignment*alignment) return 5;
    for(const auto& module:entries) {
        if(module.form!=ORPHEUS_FORM_MODULE) continue;
        size_t module_end=0,module_alignment=1;
        const std::string prefix=std::string(module.name)+"__";
        for(const auto& entry:entries) {
            if(!entry.node || std::string(entry.node).compare(0,prefix.size(),prefix)) continue;
            const auto* descriptor=runtime.get_interface(entry.node)->get_descriptor();
            uintptr_t state=(uintptr_t)entry.base-entry.offset;
            if(state<(uintptr_t)module.base) return 5;
            module_end=std::max(module_end,(size_t)(state-(uintptr_t)module.base)+descriptor->state_size);
            module_alignment=std::max(module_alignment,descriptor->alignment);
        }
        if((uintptr_t)module.base%module_alignment
            || module.byte_size!=(module_end+module_alignment-1)/module_alignment*module_alignment)
            return 5;
    }
'''


def build_operator_graph(tmp_path, operation, *, channels=1, frames=8, frame=0,
                         integer=False, bits=0, recovery=False, nested=False, alignment=None):
    def parameter(default, maximum):
        return {**scalar("int", default), "range": [1, maximum],
                "update_policy": "restart_required", "affects_signature": True}

    source_port = (
        {"direction": "output", "type": "bulk", "sample_format": "f32",
         "channels": 1, "block_size": 1}
        if operation == 3 else audio("output", channels="param:channels")
    )
    if operation == 2:
        source_port["block_size"] = "param:frames"
    source = create_component(tmp_path, "operator_source", {
        "a": {**source_port, "drive_port_no": 1},
        "b": {**source_port, "drive_port_no": 2},
        "channels": parameter(channels, 128),
        "frames": parameter(frames, 65536 if operation == 2 else 8),
        "operation": scalar("int", operation), "bits": scalar("int", bits),
        "mode": scalar("int", 0), "calls": scalar("int", 0),
    }, SOURCE, source=True)
    ports = channels if operation == 2 else 1
    capture_channels = channels if operation < 2 else 1
    capture_port = (
        {"direction": "input", "type": "bulk",
         "sample_format": "i32" if integer else "f32", "channels": 1, "block_size": 1}
        if operation >= 2 else audio("input", channels="param:channels")
    )
    comparison = (
        "if(io->in[port].samples[i]!=(int32_t)expected) return ORPHEUS_ERR_UNSUPPORTED;"
        if integer else
        "if(memcmp(&io->in[port].samples[i],&expected,sizeof(float))) return ORPHEUS_ERR_UNSUPPORTED;"
    )
    capture = create_component(tmp_path, "operator_capture", {
        "in": {**capture_port, "process": True, "count": "param:ports"},
        "ports": parameter(ports, 32), "channels": parameter(capture_channels, 128),
        "source_channels": scalar("int", channels), "frames": scalar("int", frames),
        "operation": scalar("int", operation), "bits": scalar("int", bits),
        "recovery": scalar("bool", recovery),
        "trace": scalar("int", 0), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "total": scalar("float", 0.0),
        "selected_frame": scalar("int", frame),
        "frame": scalar("int", 0), "epoch": scalar("int", 0),
    }, CAPTURE.replace("@COMPARE@", comparison))
    if alignment:
        (capture.parent / "user" / "algorithm.h").write_text(
            "#pragma once\n"
            "typedef struct orpheus_test_operator_capture_AlgorithmState {\n"
            "#ifdef __cplusplus\n"
            f"    alignas({alignment}) unsigned char history;\n"
            "#else\n"
            f"    _Alignas({alignment}) unsigned char history;\n"
            "#endif\n"
            "} orpheus_test_operator_capture_AlgorithmState;\n",
            encoding="utf-8", newline="\n")
    name = ("negate", "sample_product", "audio_sample", "scalar_to_i32")[operation]
    settings = (
        {"channels": channels, "frames": frames, "frame": frame} if operation == 2
        else {"channels": channels} if operation < 2 else {}
    )
    nodes = {
        "source": Node("source", "orpheus.test.operator_source"),
        "processor": Node("processor", f"orpheus.builtin.{name}", params=settings),
        "capture": Node("capture", "orpheus.test.operator_capture"),
    }
    pairs = [("source:a", "processor:in")]
    if operation == 1:
        pairs.append(("source:b", "processor:gain"))
    if operation == 2:
        for channel in range(channels):
            output = f"processor:out{channel}"
            if integer:
                node_id = f"convert{channel}"
                nodes[node_id] = Node(node_id, "orpheus.builtin.scalar_to_i32")
                pairs.append((output, f"{node_id}:in"))
                output = f"{node_id}:out"
            pairs.append((output, f"capture:in{channel}"))
    else:
        pairs.append(("processor:out", "capture:in0"))
    if nested:
        renamed = {"processor": "chain__inner__processor", "capture": "chain__capture"}
        nodes = {renamed.get(key, key): node for key, node in nodes.items()}
        for key, node in nodes.items():
            node.id = key
        pairs = [
            tuple(f"{renamed.get(endpoint.split(':')[0], endpoint.split(':')[0])}:"
                  f"{endpoint.split(':')[1]}" for endpoint in pair)
            for pair in pairs
        ]
    manifests = [source, capture, ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"]
    if operation == 2 and integer:
        manifests.append(ROOT / "components" / "orpheus" / "builtin" / "scalar_to_i32" / "component.yaml")
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes=nodes,
        connections=[Connection(PortRef.parse(a), PortRef.parse(b)) for a, b in pairs],
    ))
    harness = "#ifdef DYNAMIC\n#include <algorithm>\n#endif\n" + HARNESS.replace("%.9g", "%.17g")
    setter = '    if(runtime.set_parameter("source","mode",value)) return 3;'
    assert setter in harness
    harness = harness.replace(setter, ARENA_CHECK + setter)
    if nested:
        harness = harness.replace('"capture"', '"chain__capture"')
    return compile_delivery_backends(tmp_path, registry, project, manifests, harness)


@pytest.mark.parametrize("alignment", [None, 32, 64])
def test_nested_operator_arena_layout(tmp_path, alignment):
    case = build_operator_graph(tmp_path, 0, nested=True, alignment=alignment)
    expected = expected_rows(0, 1)
    for backend in ("dynamic", "generated_backend"):
        result, rows = run_backend(case, backend)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == expected


def expected_rows(operation, channels, *, frames=8, frame=0, integer=False, bits=0):
    trace = calls = 0
    total = 0.0
    rows = []
    events = 2 if operation == 1 else channels if operation == 2 else 1
    for tick in range(2):
        for port in range(events):
            if operation == 3:
                value = struct.unpack("<f", struct.pack("<i", bits))[0]
                values = [math.trunc(value)]
            else:
                indices = (
                    [frame * channels + port] if operation == 2
                    else range(frames * channels)
                )
                values = [(i % 17 - 4) * 0.25 + tick * 0.5 for i in indices]
                if operation == 0:
                    values = [-value for value in values]
                if operation == 1:
                    factor = tick + 2 if port else tick + 1 if tick else 0
                    values = [value * (i % 3 + 1) * factor for i, value in enumerate(values)]
                if integer:
                    values = [math.trunc(value) for value in values]
            for value in values:
                total = struct.unpack("<f", struct.pack("<f", total + value))[0]
            last = values[0]
            trace = (trace * 33 + (port + 1 if operation == 2 else 1)) % 100003
            calls += 1
        rows.append([tick, 0, trace, calls, last, total, tick * 8, 1])
    return rows


@pytest.mark.parametrize("operation,channels,frames,frame,integer", [
    (0, 1, 8, 0, False), (0, 64, 8, 0, False), (0, 3, 4, 0, False),
    (1, 1, 8, 0, False), (1, 128, 8, 0, False),
    (2, 1, 8, 0, False), (2, 3, 8, 7, False), (2, 32, 8, 7, False),
    (2, 3, 8, 7, True),
    (2, 3, 17, 16, False), (2, 1, 1, 0, False),
])
def test_basic_operator_real_delivery(tmp_path, operation, channels, frames, frame, integer):
    backends = build_operator_graph(
        tmp_path, operation, channels=channels, frames=frames, frame=frame, integer=integer)
    expected = expected_rows(
        operation, channels, frames=frames, frame=frame, integer=integer)
    for executable in ("dynamic", "generated_backend"):
        result, rows = run_backend(backends, executable)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == expected


@pytest.mark.parametrize("raw", [
    0x00000000, 0x80000000, 0x3FE00000, 0xBFE00000, 0xCF000000, 0x4EFFFFFF,
    0x4F000000, 0xCF000001, 0x7F800000, 0xFF800000, 0x7FC12345, 0x7FA23456,
])
def test_scalar_conversion_boundaries(tmp_path, raw):
    bits = struct.unpack("<i", struct.pack("<I", raw))[0]
    value = struct.unpack("<f", struct.pack("<I", raw))[0]
    valid = math.isfinite(value) and -(2**31) <= value < 2**31
    backends = build_operator_graph(tmp_path, 3, integer=True, bits=bits)
    for executable in ("dynamic", "generated_backend"):
        result, rows = run_backend(backends, executable)
        if valid:
            assert result.returncode == 0, result.stdout + result.stderr
            assert rows == expected_rows(3, 1, integer=True, bits=bits)
        else:
            assert result.returncode != 0
            assert rows == [[tick, -1, 0, 0, 0, 0, 0, 0] for tick in range(2)]


def test_audio_sample_rejects_out_of_frame_index(tmp_path):
    backends = build_operator_graph(tmp_path, 2, channels=3, frame=8)
    for executable in ("dynamic", "generated_backend"):
        result, rows = run_backend(backends, executable)
        assert result.returncode == 2
        assert rows == []


def test_scalar_conversion_error_does_not_block_later_delivery(tmp_path):
    backends=build_operator_graph(tmp_path,3,integer=True,bits=0x7FC12345,recovery=True)
    for executable in ("dynamic","generated_backend"):
        result,rows=run_backend(backends,executable,mode=5)
        assert result.returncode==0,result.stdout+result.stderr
        assert rows==[[0,-1,0,0,0,0,0,0],[1,0,1,1,1,1,8,1]]
