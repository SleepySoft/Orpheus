"""电平探针对每次实际交付测量、直通，并保留只读与控制源语义。"""

import ctypes as C
import math

import pytest

from orpheus_core.component_codegen import assert_component_generated_current
from orpheus_core.component_definition import load_component_definition
from orpheus_core.project import Connection, ControlConnection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_biquad_bank_interfaces import register
from test_dynamic_interface_delivery import run_backend
from test_filters_delay_interfaces import Buffer, Context, Continuation, Emit, Instance, SR
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar


NAMES = ("probe_peak", "probe_rms")


def metric(name, samples):
    value = max(abs(sample) for sample in samples) if name == "probe_peak" else math.sqrt(
        math.fsum(sample * sample for sample in samples) / len(samples))
    return C.c_float(value).value


@pytest.mark.parametrize("name", NAMES)
def test_probe_manifest_and_read_only_slot(name):
    directory = ROOT / "components" / "orpheus" / "builtin" / name
    raw = load_component_definition(directory / "component.yaml")
    assert raw["abi_version"] == 7
    assert not ({"ports", "parameters", "sources", "headers", "state_type"} & raw.keys())
    assert raw["interfaces"]["in"]["process"] is True
    assert raw["interfaces"]["out"]["drive_port_no"] == 1
    key = "peak" if name == "probe_peak" else "rms"
    assert raw["interfaces"][key]["access"] == "read"
    assert raw["interfaces"][key].get("control_source", False) == (name == "probe_rms")
    assert_component_generated_current(directory / "component.yaml")
    instance = Instance(name)
    try:
        assert instance.prepare() == 0
        _, count, size, flags, kind = register(instance)[key]
        assert (count, size, kind) == (1, 4, 3)
        assert flags & 2 and not flags & 1
        assert instance.set(key, 99.0) != 0
        assert instance.get(key) == 0
    finally:
        instance.close()


@pytest.mark.parametrize("name", NAMES)
@pytest.mark.parametrize("channels", [1, 2, 32])
@pytest.mark.parametrize("frames", [1, 7, 128])
@pytest.mark.parametrize("inplace", [False, True])
def test_probe_native_numeric_reset_and_passthrough(name, channels, frames, inplace):
    instance = Instance(name, channels=channels)
    key = "peak" if name == "probe_peak" else "rms"
    samples = [(index % 11 - 5) * 0.25 for index in range(frames * channels)]
    try:
        assert instance.prepare() == 0
        assert instance.run(samples, inplace=inplace) == samples
        assert instance.get(key) == metric(name, samples)
        assert instance.set(key, 99.0) != 0
        assert instance.get(key) == metric(name, samples)
        assert instance.api.reset(instance.state) == 0
        assert instance.get(key) == 0
        assert instance.run([0.0] * len(samples), inplace=inplace) == [0.0] * len(samples)
        assert instance.get(key) == 0
        instance.run(samples, inplace=inplace, emit_result=-73)
        assert instance.get(key) == metric(name, samples)
        assert instance.prepare() == 0
        assert instance.get(key) == 0
    finally:
        instance.close()


def invoke_raw(instance, words, *, inplace=False, overlap=None, input_null=False,
               output_null=False, capacity=None):
    frames = len(words) // instance.channels
    source = (C.c_uint32 * max(1, len(words)))(*words)
    output = source if inplace else (C.c_uint32 * max(1, len(words)))(
        *([0xDEADBEEF] * max(1, len(words))))
    if overlap is not None:
        shared = (C.c_uint32 * (len(words) + abs(overlap)))()
        address = C.addressof(shared) + max(0, -overlap) * C.sizeof(C.c_uint32)
        source = (C.c_uint32 * len(words)).from_address(address)
        output = (C.c_uint32 * len(words)).from_address(address + overlap * C.sizeof(C.c_uint32))
        source[:] = words
    ib = Buffer(C.addressof(source), 0, instance.channels, frames, frames, True)
    ob = Buffer(C.addressof(output), 0, instance.channels,
                frames if capacity is None else capacity, 0, True)
    inputs = (C.POINTER(Buffer) * 1)(C.POINTER(Buffer)() if input_null else C.pointer(ib))
    outputs = (C.POINTER(Buffer) * 1)(C.POINTER(Buffer)() if output_null else C.pointer(ob))
    deliveries = []

    @Emit
    def emitted(_, port, delivery):
        data = delivery.contents
        deliveries.append((port, data.buffer.contents.frame_count, data.frame_index, data.epoch))
        return 0

    continuation = Continuation(None, None, 1, emitted)
    context = Context(state=instance.state, inputs=inputs, outputs=outputs,
                      input_count=1, output_count=1, frame_count=frames, sample_rate=SR,
                      frame_index=24, epoch=3, valid_frames=frames,
                      continuation=C.pointer(continuation), trigger_input=0)
    result = instance.api.process(instance.state, C.byref(context))
    return result, list(output), deliveries


@pytest.mark.parametrize("name", NAMES)
@pytest.mark.parametrize("overlap", [-1, 1])
def test_probe_overlapping_views_measure_before_copy(name, overlap):
    words = [0x3F800000, 0xBF800000, 0x40000000, 0xC0400000]
    instance = Instance(name, frames=4)
    key = "peak" if name == "probe_peak" else "rms"
    try:
        assert instance.prepare() == 0
        result, output, deliveries = invoke_raw(instance, words, overlap=overlap)
        assert result == 0 and output == words
        assert deliveries == [(1, 4, 24, 3)]
        assert instance.get(key) == metric(name, [1.0, -1.0, 2.0, -3.0])
    finally:
        instance.close()


@pytest.mark.parametrize("name", NAMES)
@pytest.mark.parametrize("inplace", [False, True])
def test_probe_preserves_raw_nonfinite_and_zero_bits(name, inplace):
    words = [0, 0x80000000, 0x7FC12345, 0x7FA23456, 0x7F800000,
             0xFF800000, 1, 0xBF800000]
    instance = Instance(name, frames=8)
    try:
        assert instance.prepare() == 0
        result, output, deliveries = invoke_raw(instance, words, inplace=inplace)
        assert result == 0 and output == words
        assert deliveries == [(1, 8, 24, 3)]
        if name == "probe_peak":
            assert instance.get("peak") == math.inf
        else:
            assert math.isnan(instance.get("rms"))
    finally:
        instance.close()


@pytest.mark.parametrize("name", NAMES)
def test_probe_zero_quantum_and_invalid_buffers_preserve_measurement(name):
    instance = Instance(name, frames=4)
    key = "peak" if name == "probe_peak" else "rms"
    try:
        assert instance.prepare() == 0
        instance.run([1.25] * 4)
        result, output, deliveries = invoke_raw(instance, [])
        assert result == 0 and output == [0xDEADBEEF] and deliveries == []
        assert instance.get(key) == 1.25
        for invalid in ({"input_null": True}, {"output_null": True}, {"capacity": 3}):
            result, output, deliveries = invoke_raw(instance, [0x40000000] * 4, **invalid)
            assert result != 0 and output == [0xDEADBEEF] * 4 and deliveries == []
            assert instance.get(key) == 1.25
    finally:
        instance.close()


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_meter_source_algorithm(orpheus_test_meter_source_State* s,
    orpheus_test_meter_source_IO* io, const OrpheusProcessContext* ctx) {
    for(int arrival=0;arrival<2;arrival++) {
        int step=s->tick*2+arrival+1;
        for(int i=0;i<s->frames*s->channels;i++)
            io->out.samples[i]=step*(i%5-2)*0.25f;
        OrpheusBuffer buffer=*io->out.buffer;
        buffer.frame_count=(uint32_t)s->frames;
        orpheus_test_meter_source_OutputF32 output=io->out;
        output.buffer=&buffer; output.delivery.buffer=&buffer;
        int result=orpheus_test_meter_source_emit_out(ctx,&output);
        if(result) return result;
    }
    s->tick++;
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
#include <math.h>
int orpheus_test_meter_capture_algorithm(orpheus_test_meter_capture_State* s,
    orpheus_test_meter_capture_IO* io, const OrpheusProcessContext* ctx) {
    if(ctx->frame_count!=(uint32_t)s->frames || ctx->valid_frames!=ctx->frame_count
        || ctx->frame_index!=(uint64_t)(s->calls/2*8) || ctx->epoch!=1)
        return ORPHEUS_ERR_UNSUPPORTED;
    double measured_sum=0;
    for(int i=0;i<s->frames*s->channels;i++) {
        float sample=(s->calls+1)*(i%5-2)*0.25f;
        if(io->in.samples[i]!=sample) return ORPHEUS_ERR_UNSUPPORTED;
        measured_sum+=(double)sample*sample;
    }
    if(s->control && s->observed!=(float)sqrt(measured_sum/(s->frames*s->channels)))
        return ORPHEUS_ERR_UNSUPPORTED;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''

HARNESS = r'''
#include <stdio.h>
#ifdef DYNAMIC
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime runtime;
static double get(const char* node,const char* key) {
    OrpheusValue v={};
    if(runtime.get_parameter(node,key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? v.value.f32 : v.value.i32;
}
#else
#include "orpheus_graph.h"
#include "orpheus_control.h"
static double get(const char* node,const char* key) {
    OrpheusValue v={0};
    if(orpheus_control_get_value(node,key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? v.value.f32 : v.value.i32;
}
#endif
int main(int argc,char** argv) {
#ifdef DYNAMIC
    if(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])) return 2;
#else
    if(orpheus_generated_init(48000,8)) return 2;
#endif
    for(int tick=0;tick<2;tick++) {
#ifdef DYNAMIC
        int result=runtime.process_task("task_source",8);
#else
        int result=orpheus_generated_process_task_task_source(8);
#endif
        if(result) return 3;
        printf("%d %.17g %.17g %.17g %.17g\n",tick,get("capture","calls"),
            get("meter","@METRIC@"),get("capture","observed"),get("capture","frame"));
    }
    return 0;
}
'''


@pytest.mark.parametrize("name", NAMES)
@pytest.mark.parametrize("channels", [1, 2, 32])
@pytest.mark.parametrize("frames", [3, 8])
def test_probe_real_deliveries_and_rms_control_before_each_call(tmp_path, name, channels, frames):
    source = create_component(tmp_path, "meter_source", {
        "out": audio("output", channels=channels, drive_port_no=1),
        "frames": scalar("int", frames), "channels": scalar("int", channels),
        "tick": scalar("int", 0),
    }, SOURCE, source=True)
    control = name == "probe_rms"
    capture = create_component(tmp_path, "meter_capture", {
        "in": audio("input", channels=channels, process=True),
        "frames": scalar("int", frames), "channels": scalar("int", channels),
        "control": scalar("bool", control), "calls": scalar("int", 0),
        "frame": scalar("int", 0),
        "observed": {**scalar("float", 0.0), "bindable": True,
                     "access": "read_write", "update_policy": "block_boundary"},
    }, CAPTURE)
    meter = ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
    manifests = [source, capture, meter]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={
            "source": Node("source", "orpheus.test.meter_source"),
            "meter": Node("meter", f"orpheus.builtin.{name}", params={"channels": channels}),
            "capture": Node("capture", "orpheus.test.meter_capture"),
        },
        connections=[
            Connection(PortRef("source", "out"), PortRef("meter", "in")),
            Connection(PortRef("meter", "out"), PortRef("capture", "in")),
        ],
    ), control_connections=[
        ControlConnection(PortRef("meter", "rms"), PortRef("capture", "observed"))
    ] if control else [])
    key = "peak" if name == "probe_peak" else "rms"
    case = compile_delivery_backends(
        tmp_path, registry, project, manifests, HARNESS.replace("@METRIC@", key))
    if control:
        assert case[2].control_links[0]["delivery"] == "before_call"
    expected = []
    for tick in range(2):
        value = metric(name, [(tick * 2 + 2) * (index % 5 - 2) * 0.25
                              for index in range(frames * channels)])
        expected.append([tick, (tick + 1) * 2, value, value if control else 0, tick * 8])
    for backend in ("dynamic", "generated_backend"):
        result, rows = run_backend(case, backend)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == expected
