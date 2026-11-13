"""双路合流逐位数值与调用节拍：rate_sync 迁入接口声明后锁定动态/生成一致。"""

import subprocess

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project, Task
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar

HARNESS = r'''
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
int main(int argc, char** argv) {
    (void)argc; (void)argv;
#ifdef DYNAMIC
    if (runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) != 0) return 2;
#else
    if (orpheus_graph_init(48000, 8) != 0) return 2;
#endif
    for (int tick = 0; tick < 300; ++tick) {
#ifdef DYNAMIC
        if (runtime.process_block(8) != 0) return 3;
#else
        if (orpheus_graph_process(8) != 0) return 3;
#endif
    }
    OrpheusValue calls = {ORPHEUS_VALUE_INT, {0}}, frames = {ORPHEUS_VALUE_INT, {0}};
#ifdef DYNAMIC
    if (runtime.get_parameter("capture", "calls", &calls) != 0) return 4;
    if (runtime.get_parameter("capture", "frames", &frames) != 0) return 4;
#else
    if (orpheus_control_get_value("capture", "calls", &calls) != 0) return 4;
    if (orpheus_control_get_value("capture", "frames", &frames) != 0) return 4;
#endif
    if (calls.value.i32 != 25 || frames.value.i32 != 2400) return 5;
#ifndef DYNAMIC
    orpheus_graph_teardown();
#endif
    printf("%d %d\n", calls.value.i32, frames.value.i32);
    return 0;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>
#define TWO_PI (2.0f*3.14159265358979f)
int orpheus_test_merge_capture_algorithm(orpheus_test_merge_capture_State* s,
    orpheus_test_merge_capture_IO* io, const OrpheusProcessContext* ctx) {
    /* 合流量子=96帧：两源每量子各产96帧(A 4×24 / B 3×32)，同步点整块到达；
       首拍不连续，时间线步长96，epoch 初始为1。 */
    if (ctx->trigger_input != io->in.input_index || !io->in.buffer || ctx->frame_count != 96)
        return ORPHEUS_ERR_INVALID_ARG;
    if (s->calls >= 25) return ORPHEUS_ERR_PROCESSING;
    if (ctx->frame_index != (uint64_t)s->calls * 96u || ctx->epoch != 1 ||
        !!(ctx->timeline_flags & ORPHEUS_TIMELINE_DISCONTINUITY) != (s->calls == 0))
        return ORPHEUS_ERR_PROCESSING;
    /* 逐位复现 signal_gen：步长表达式与浮点次序保持一致。
       桥接深度=96=每量子产量，两路窗口同为[96k, 96k+96)，逐帧对齐相加。 */
    float step_a = TWO_PI*s->fa/ctx->sample_rate;
    float step_b = TWO_PI*s->fb/ctx->sample_rate;
    for (uint32_t j = 0; j < 96; ++j) {
        float av = s->aa*sinf(s->phase_a);
        s->phase_a += step_a;
        if (s->phase_a > TWO_PI) s->phase_a -= TWO_PI;
        float bv = s->ab*sinf(s->phase_b);
        s->phase_b += step_b;
        if (s->phase_b > TWO_PI) s->phase_b -= TWO_PI;
        float expected = av + bv;
        if (memcmp(io->in.samples + j, &expected, 4)) return ORPHEUS_ERR_PROCESSING;
    }
    s->frames += 96;
    ++s->calls;
    return ORPHEUS_OK;
}
'''


def test_rate_sync_merge_sum_two_backends(tmp_path):
    capture = create_component(tmp_path, "merge_capture", {
        "in": audio("input", channels=1, process=True),
        "calls": scalar("int", 0), "frames": scalar("int", 0),
        "fa": scalar("float", 100.0), "fb": scalar("float", 300.0),
        "aa": scalar("float", 0.25), "ab": scalar("float", 0.125),
        "phase_a": scalar("float", 0.0), "phase_b": scalar("float", 0.0),
    }, CAPTURE)
    manifests = [ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                 for name in ("signal_gen", "rate_sync")]
    manifests.append(capture)
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(metadata={"name": "merge"}, block_size=8, sample_rate=48000)
    project.tasks = {
        "tidA": Task(id="tidA", sample_rate=48000, block_size=24),
        "tidB": Task(id="tidB", sample_rate=48000, block_size=32),
    }
    project.graph = Graph(nodes={
        "a": Node(id="a", component="orpheus.builtin.signal_gen", task="tidA",
                  params={"frequency": 100.0, "amplitude": 0.25, "channels": 1}),
        "b": Node(id="b", component="orpheus.builtin.signal_gen", task="tidB",
                  params={"frequency": 300.0, "amplitude": 0.125, "channels": 1}),
        "sync": Node(id="sync", component="orpheus.builtin.rate_sync", task="tidA",
                     params={"channels": 1, "mode": 0, "buffer_length": 0}),
        "capture": Node(id="capture", component="orpheus.test.merge_capture", task="tidA"),
    }, connections=[
        Connection(PortRef("a", "out"), PortRef("sync", "in0")),
        Connection(PortRef("b", "out"), PortRef("sync", "in1")),
        Connection(PortRef("sync", "out"), PortRef("capture", "in")),
    ])
    directory, plan_path, plan, _, _ = compile_delivery_backends(tmp_path, registry, project, manifests, HARNESS)
    assert plan.node_configs["sync"]["block_size"] == 96
    assert plan.schedule["periods"]["sync"] == 12
    for backend in ("dynamic", "generated_backend"):
        arguments = [str(plan_path), str(directory / "build" / "components")] if backend == "dynamic" else []
        result = subprocess.run([str(directory / "build" / (backend + ".exe")), *arguments],
                                capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, (backend, result.stdout, result.stderr)
        assert result.stdout.strip() == "25 2400"
