"""不打开硬件：模拟宿主填充/读取设备缓冲，验证短块与真实交付。"""

import subprocess

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DYNAMIC
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime runtime;
#else
#include "orpheus_graph.h"
#include "orpheus_control.h"
#endif
int main(int argc,char** argv) {
    int task=atoi(argv[argc-1]);
#ifdef DYNAMIC
    assert(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])==0);
    OrpheusBuffer* input=runtime.get_output_buffer("source","out");
    const OrpheusBuffer* output=runtime.get_input_buffer("sink","in");
#else
    assert(orpheus_graph_init(@RATE@,@FRAMES@)==0);
    OrpheusBuffer* input=orpheus_host_device_in_buffer();
    const OrpheusBuffer* output=orpheus_host_device_out_buffer();
#endif
    assert(input && output && input->channels==@CHANNELS@ && output->channels==@CHANNELS@);
    const uint32_t lengths[]={@FRAMES@,@FRAMES@/3,1,@FRAMES@-1,@FRAMES@,2};
    uint64_t position=0;
    for(unsigned call=0;call<sizeof(lengths)/sizeof(lengths[0]);++call) {
        uint32_t frames=lengths[call];
        float* samples=(float*)input->data;
        for(unsigned n=0;n<frames;++n) for(unsigned c=0;c<@CHANNELS@;++c)
            samples[n*@CHANNELS@+c]=(float)((int)(((position+n)*17+c*31)%103)-51)*.03125f;
        samples[0]=-0.0f;
        input->frame_count=frames;
#ifdef DYNAMIC
        int rc=task ? runtime.process_task("task_source",frames) : runtime.process_block(frames);
#else
        int rc=task ? orpheus_graph_process_task_task_source(frames) : orpheus_graph_process(frames);
#endif
        assert(rc==0 && output->frame_count==frames);
        const float* result=(const float*)output->data;
        for(unsigned i=0;i<frames*@CHANNELS@;++i) {
            float expected=samples[i]*2.0f;
            assert(memcmp(result+i,&expected,4)==0);
        }
        position+=frames;
        OrpheusValue calls={ORPHEUS_VALUE_INT,{0}}, total={ORPHEUS_VALUE_INT,{0}};
#ifdef DYNAMIC
        assert(runtime.get_parameter("capture","calls",&calls)==0);
        assert(runtime.get_parameter("capture","frames",&total)==0);
#else
        assert(orpheus_control_get_value("capture","calls",&calls)==0);
        assert(orpheus_control_get_value("capture","frames",&total)==0);
#endif
        assert(calls.value.i32==(int32_t)call+1 && total.value.i32==(int32_t)position);
    }
#ifndef DYNAMIC
    orpheus_graph_teardown();
#endif
    printf("%llu\n",(unsigned long long)position);
    return 0;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_device_capture_algorithm(orpheus_test_device_capture_State* s,
    orpheus_test_device_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(ctx->frame_index!=(uint64_t)s->frames || ctx->sample_rate!=@RATE@ ||
       ctx->valid_frames!=ctx->frame_count || ctx->epoch!=1 ||
       !!(ctx->timeline_flags&ORPHEUS_TIMELINE_DISCONTINUITY)!=(s->calls==0))
        return ORPHEUS_ERR_PROCESSING;
    for(unsigned n=0;n<ctx->frame_count;++n) for(unsigned c=0;c<@CHANNELS@;++c) {
        float expected=(float)((int)(((s->frames+n)*17+c*31)%103)-51)*.0625f;
        if(!n && !c) expected=-0.0f;
        if(memcmp(io->in.samples+n*@CHANNELS@+c,&expected,4)) return ORPHEUS_ERR_PROCESSING;
    }
    s->frames+=(int32_t)ctx->frame_count;
    ++s->calls;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels,frames,rate,requested", [(3, 8, 48000, 0), (32, 128, 32000, 32000)])
def test_device_host_chunks_and_time_four_entries(tmp_path, channels, frames, rate, requested):
    capture = create_component(tmp_path, "device_capture", {
        "in": audio("input", channels=channels, process=True),
        "frames": scalar("int", 0), "calls": scalar("int", 0),
    }, CAPTURE.replace("@CHANNELS@", str(channels)).replace("@RATE@", str(rate)))
    manifests = [ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                 for name in ("device_in", "device_out", "linear_gain")]
    manifests.append(capture)
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, sample_rate=rate, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.device_in",
                       params={"channels": channels, "sample_rate": requested}),
        "gain": Node("gain", "orpheus.builtin.linear_gain", params={"channels": channels, "gain": 2.0}),
        "sink": Node("sink", "orpheus.builtin.device_out", params={"channels": channels, "sample_rate": requested}),
        "capture": Node("capture", "orpheus.test.device_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("gain", "in")),
        Connection(PortRef("gain", "out"), PortRef("sink", "in")),
        Connection(PortRef("gain", "out"), PortRef("capture", "in")),
    ]))
    harness = HARNESS.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)).replace("@RATE@", str(rate))
    directory, plan_path, plan, _, _ = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert plan.node_configs["source"]["output_drive_ports"] == {"out": 1}
    assert plan.node_configs["sink"]["input_ports"] == ["in"]
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            arguments = [str(plan_path), str(directory / "build" / "components")] if backend == "dynamic" else []
            result = subprocess.run([str(directory / "build" / (backend + ".exe")), *arguments, str(mode)],
                                    capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, (backend, mode, result.stdout, result.stderr)
            assert int(result.stdout.strip()) == frames * 3 + frames // 3 + 2
