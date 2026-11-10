"""同一 Task 内重复交付必须分别推进逐元素历史。"""

import ctypes
import re
import struct
import subprocess

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_bulk_arithmetic_interfaces import HARNESS
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_state_source_algorithm(orpheus_test_state_source_State* s,
    orpheus_test_state_source_IO* io,const OrpheusProcessContext* ctx) {
    for(int repeat=0;repeat<2;repeat++) {
        int event=s->calls++;
        if(io->update.buffer) {
            io->update.samples[0]=event!=1;
            io->update.buffer->frame_count=1;
        }
        if(io->bound.buffer) {
            io->bound.samples[0]=(float)(event%2);
            io->bound.buffer->frame_count=1;
        }
        for(int i=0;i<@COUNT@;i++)
            io->out.samples[i]=((i*17+event*13)%129-64)*0.03125f;
        io->out.buffer->frame_count=3;
        int result=orpheus_test_state_source_emit_out(ctx,&io->out);
        if(result) return result;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
static const uint32_t expected[4][@COUNT@]={@EXPECTED@};
int orpheus_test_stateful_capture_algorithm(orpheus_test_stateful_capture_State* s,
    orpheus_test_stateful_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(s->calls>=4 || ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/2*8)) return ORPHEUS_ERR_UNSUPPORTED;
    for(int i=0;i<@COUNT@;i++) {
        uint32_t bits;memcpy(&bits,io->in.samples+i,sizeof(bits));
        if(bits!=expected[s->calls][i]) return ORPHEUS_ERR_UNSUPPORTED;
    }
    s->last=io->in.samples[@COUNT@-1];s->frame=(int32_t)ctx->frame_index;s->calls++;
    return ORPHEUS_OK;
}
'''


def f32(value):
    return ctypes.c_float(value).value


def expected_deliveries(component, count):
    previous = [0.0 if component == "bulk_one_pole" else f32(0.125)] * count
    deltas, changes = [0.0] * count, [0.0] * count
    alpha = f32(0.61)
    attack, decay, threshold, slow = map(f32, (0.3, -0.4, 0.65, 0.07))
    expected = []
    for event in range(4):
        for i in range(count):
            value = ((i * 17 + event * 13) % 129 - 64) * 0.03125
            if component == "bulk_one_pole":
                if event != 1:
                    previous[i] = f32(f32(f32(1 - alpha) * value) + f32(previous[i] * alpha))
            else:
                delta = f32(value - previous[i])
                change = min(attack, delta) if delta > 0 else max(decay, delta)
                slew = changes[i] if f32(deltas[i] * delta) > 0 else 0.0
                if event % 2 and slew == 0:
                    slew = min(slow, delta) if delta > 0 else max(-slow, delta)
                if abs(delta) <= threshold and abs(slew) < abs(change):
                    change = slew
                previous[i] = f32(previous[i] + change)
                deltas[i], changes[i] = delta, change
        expected.append(previous.copy())
    return expected


@pytest.mark.parametrize("component", ["bulk_one_pole", "bulk_guarded_slew"])
@pytest.mark.parametrize("channels", [1, 128])
def test_each_delivery_advances_history_with_same_task_timestamp(tmp_path, component, channels):
    count = channels * 3
    expected = expected_deliveries(component, count)
    table = ",".join("{" + ",".join(
        f"0x{struct.unpack('<I', struct.pack('<f', value))[0]:08x}u" for value in row
    ) + "}" for row in expected)
    vector = {"type": "bulk", "sample_format": "f32", "channels": channels, "block_size": 3}
    side = {"direction": "output", "type": "bulk", "channels": 1, "block_size": 1}
    source = create_component(tmp_path, "state_source", {
        "out": {**vector, "direction": "output", "drive_port_no": 1},
        "update": {**side, "sample_format": "i32"},
        "bound": {**side, "sample_format": "f32"},
        "calls": scalar("int", 0),
    }, SOURCE.replace("@COUNT@", str(count)), source=True)
    capture = create_component(tmp_path, "stateful_capture", {
        "in": {**vector, "direction": "input", "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@COUNT@", str(count)).replace("@EXPECTED@", table))
    processor = ROOT / "components" / "orpheus" / "builtin" / component / "component.yaml"
    registry = Registry()
    for manifest in (source, capture, processor):
        registry.add_manifest(manifest)
    parameters = {"channels": channels, "elements": 3}
    if component == "bulk_one_pole":
        parameters["alpha"] = 0.61
        side_name = "update"
    else:
        parameters.update(attack=0.3, decay=-0.4, threshold=0.65, slow=0.07, initial=0.125)
        side_name = "bound"
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={
            "source": Node("source", "orpheus.test.state_source"),
            "processor": Node("processor", f"orpheus.builtin.{component}", params=parameters),
            "capture": Node("capture", "orpheus.test.stateful_capture"),
        },
        connections=[
            Connection(PortRef("source", "out"), PortRef("processor", "in")),
            Connection(PortRef("source", side_name), PortRef("processor", side_name)),
            Connection(PortRef("processor", "out"), PortRef("capture", "in")),
        ],
    ))
    case = compile_delivery_backends(tmp_path, registry, project, [source, processor, capture], HARNESS)
    for backend in ("dynamic", "generated_backend"):
        result, rows = run_backend(case, backend)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == [[tick, (tick + 1) * 2, expected[tick * 2 + 1][-1], tick * 8]
                        for tick in range(2)]


ALLOCATOR_HARNESS = r'''
#undef calloc
#undef free
#include "component_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}}while(0)
static int live,fail_allocation,processing;
void* tracked_calloc(size_t n,size_t size) {
    if(processing) abort();
    if(fail_allocation) return NULL;
    void* memory=calloc(n,size);if(memory) live++;return memory;
}
void tracked_free(void* memory) {
    if(processing) abort();
    if(memory) live--;
    free(memory);
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    @PREFIX@_State state,before;void* instance=NULL;
    const char* ids[]={"channels","elements"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_INT,.value.i32=3}};
    OrpheusConfig config={.sample_rate=48000,.block_size=8,.param_ids=ids,.param_values=values,
        .param_count=2,.state_block=&state};
    CHECK(api->create(&instance,&config)==ORPHEUS_OK && instance==&state && live==0);
    CHECK(api->prepare(instance,&config)==ORPHEUS_OK && live==1 && state.algorithm.count==3);
    float data[]={2,4,6},result[3];
    OrpheusBuffer in={.data=data,.channels=1,.frame_count=3,.frame_capacity=3,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true},out=in;
    out.data=result;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.input_count=1,.outputs=outputs,
        .output_count=1,.frame_count=8};
    processing=1;int status=api->process(instance,&context);processing=0;
    CHECK(status==ORPHEUS_OK && live==1);
    memcpy(&before,&state,sizeof(state));
    unsigned char history[3*sizeof(*state.algorithm.history)];
    memcpy(history,state.algorithm.history,sizeof(history));
    values[1].value.i32=0;
    CHECK(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG && live==1);
    CHECK(!memcmp(&before,&state,sizeof(state)) && !memcmp(history,state.algorithm.history,sizeof(history)));
    values[1].value.i32=5;fail_allocation=1;
    CHECK(api->prepare(instance,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && live==0 && !state.algorithm.history);
    processing=1;status=api->process(instance,&context);processing=0;
    CHECK(status==ORPHEUS_ERR_INVALID_ARG && live==0);
    fail_allocation=0;
    CHECK(api->prepare(instance,&config)==ORPHEUS_OK && live==1 && state.algorithm.count==5);
    float fresh[]={2,4,6,8,10},fresh_result[5];
    in.data=fresh;in.frame_count=in.frame_capacity=5;
    out.data=fresh_result;out.frame_count=0;out.frame_capacity=5;
    processing=1;status=api->process(instance,&context);processing=0;
    CHECK(status==ORPHEUS_OK && out.frame_count==5 && live==1);
    CHECK(api->reset(instance)==ORPHEUS_OK && live==1);
    CHECK(api->destroy(instance)==ORPHEUS_OK && live==0 && !state.algorithm.history);
    CHECK(api->destroy(instance)==ORPHEUS_OK && live==0);
    CHECK(api->create(&instance,&config)==ORPHEUS_OK && live==0);
    CHECK(api->destroy(instance)==ORPHEUS_OK && live==0);
    return 0;
}
'''


@pytest.mark.parametrize("component", ["bulk_one_pole", "bulk_guarded_slew"])
def test_prepare_failures_are_safe_retryable_and_process_never_allocates(tmp_path, component):
    directory = ROOT / "components" / "orpheus" / "builtin" / component
    compiler = re.search(
        r"^CMAKE_C_COMPILER:[^=]+=(.+)$",
        (ROOT / "build" / "CMakeCache.txt").read_text(encoding="utf-8"), re.M,
    )[1]
    source, executable = tmp_path / "allocator.c", tmp_path / "allocator.exe"
    source.write_text(ALLOCATOR_HARNESS.replace("@PREFIX@", f"orpheus_builtin_{component}"),
                      encoding="utf-8", newline="\n")
    command = [compiler, "-std=c11", "-DORPHEUS_API=", "-Dcalloc=tracked_calloc", "-Dfree=tracked_free",
               "-I", str(ROOT / "orpheus_abi" / "include"), "-I", str(directory / "generated"),
               "-I", str(directory / "user"), str(directory / "generated" / "component_shell.c"),
               str(directory / "user" / "algorithm.c"), str(source), "-lm", "-o", str(executable)]
    for arguments in (command, [str(executable)]):
        result = subprocess.run(arguments, capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
