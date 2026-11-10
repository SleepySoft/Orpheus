"""动态引脚按实际槽位和稳定局部出口编号交付，两路径分别对照独立预期。"""

import copy
import os
import subprocess

import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.component_definition import ComponentDefinitionError, normalize_component_definition
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import audio, compile_delivery_backends, create_component, scalar


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_dynamic_source_algorithm(orpheus_test_dynamic_source_State* s,
    orpheus_test_dynamic_source_IO* io, const OrpheusProcessContext* ctx) {
    if(io->items_count!=(uint32_t)s->ports) return ORPHEUS_ERR_INVALID_ARG;
    if(s->mode==4) {
        if(io->items_count<3)
            return orpheus_test_dynamic_source_emit_items(ctx,&io->items[io->items_count]);
        orpheus_test_dynamic_source_OutputF32 invalid=io->items[0];
        invalid.drive_port_no=8;
        return orpheus_test_dynamic_source_emit_items(ctx,&invalid);
    }
    for(uint32_t p=0;p<io->items_count;p++) {
        if(io->items[p].drive_port_no!=5+p) return ORPHEUS_ERR_INVALID_ARG;
        for(uint32_t f=0;f<ctx->frame_count;f++) io->items[p].samples[f]=(float)(p+1);
    }
    for(uint32_t f=0;f<ctx->frame_count;f++) io->tail.samples[f]=10;
    if(io->items_count) {
        int rc=orpheus_test_dynamic_source_emit_items(ctx,&io->items[io->items_count-1]);
        if(rc) return rc;
    }
    int rc=orpheus_test_dynamic_source_emit_tail(ctx,&io->tail);
    if(rc || !io->items_count) return rc;
    return orpheus_test_dynamic_source_emit_items(ctx,&io->items[0]);
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_dynamic_capture_algorithm(orpheus_test_dynamic_capture_State* s,
    orpheus_test_dynamic_capture_IO* io, const OrpheusProcessContext* ctx) {
    if(io->items_count!=(uint32_t)s->ports || io->head.input_index!=0
        || io->tail.input_index!=1+io->items_count) return ORPHEUS_ERR_INVALID_ARG;
    const float* samples=0;
    int digit=0;
    for(uint32_t i=0;i<io->items_count;i++) {
        if(io->items[i].input_index!=i+1) return ORPHEUS_ERR_INVALID_ARG;
        if(ctx->trigger_input==io->items[i].input_index) {
            samples=io->items[i].samples;
            digit=(int)i+1;
        }
    }
    if(ctx->trigger_input==io->tail.input_index) {
        samples=io->tail.samples;
        digit=9;
    }
    if(!samples || !digit) return ORPHEUS_ERR_INVALID_ARG;
    s->trace=s->trace*10+digit;
    s->calls++;
    s->last=samples[0];
    for(uint32_t f=0;f<ctx->frame_count;f++) s->total+=samples[f];
    s->frame=(int32_t)ctx->frame_index;
    s->epoch=(int32_t)ctx->epoch;
    return ORPHEUS_OK;
}
'''

TRIGGER_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_dynamic_source_algorithm(orpheus_test_dynamic_source_State* s,
    orpheus_test_dynamic_source_IO* io, const OrpheusProcessContext* ctx) {
    if(io->items_count!=(uint32_t)s->ports) return ORPHEUS_ERR_INVALID_ARG;
    if(s->mode==4) {
        if(io->items_count<3)
            return orpheus_test_dynamic_source_emit_items(ctx,&io->items[io->items_count]);
        orpheus_test_dynamic_source_OutputTrigger invalid=io->items[0];
        invalid.drive_port_no=8;
        return orpheus_test_dynamic_source_emit_items(ctx,&invalid);
    }
    if(io->items_count) {
        int rc=orpheus_test_dynamic_source_emit_items(ctx,&io->items[io->items_count-1]);
        if(rc) return rc;
    }
    int rc=orpheus_test_dynamic_source_emit_tail(ctx,&io->tail);
    if(rc || !io->items_count) return rc;
    return orpheus_test_dynamic_source_emit_items(ctx,&io->items[0]);
}
'''

TRIGGER_CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_dynamic_capture_algorithm(orpheus_test_dynamic_capture_State* s,
    orpheus_test_dynamic_capture_IO* io, const OrpheusProcessContext* ctx) {
    if(io->items_count!=(uint32_t)s->ports || io->head.input_index!=0
        || io->tail.input_index!=1+io->items_count) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusDelivery* delivery=0;
    int digit=0, value=0;
    for(uint32_t i=0;i<io->items_count;i++) {
        if(io->items[i].input_index!=i+1) return ORPHEUS_ERR_INVALID_ARG;
        if(ctx->trigger_input==io->items[i].input_index) {
            delivery=io->items[i].delivery;
            digit=value=(int)i+1;
        }
    }
    if(ctx->trigger_input==io->tail.input_index) {
        delivery=io->tail.delivery;
        digit=9; value=10;
    }
    if(!delivery || delivery!=ctx->delivery || delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    s->trace=s->trace*10+digit;
    s->calls++;
    s->last=(float)value;
    s->total+=(float)(value*ctx->frame_count);
    s->frame=(int32_t)ctx->frame_index;
    s->epoch=(int32_t)ctx->epoch;
    return ORPHEUS_OK;
}
'''


@pytest.fixture(scope="module",
                params=[(count, kind) for kind in ("audio", "bulk", "control", "trigger")
                        for count in (0, 1, 3)] + [(1, "uint32-limit")],
                ids=[f"{kind}-{count}" for kind in ("audio", "bulk", "control", "trigger")
                     for count in (0, 1, 3)] + ["uint32-limit"])
def dynamic_backends(tmp_path_factory, request):
    directory = tmp_path_factory.mktemp("dynamic_interface_delivery")
    count, kind = request.param
    base = 2**32 - 2 if kind == "uint32-limit" else 5
    maximum = 1 if kind == "uint32-limit" else 3
    kind = "audio" if kind == "uint32-limit" else kind

    def port(direction, **fields):
        result = audio(direction, **fields)
        result["type"] = kind
        if kind == "trigger":
            del result["channels"], result["sample_format"]
        elif kind != "audio":
            result["block_size"] = 8
        return result

    parameter = {**scalar("int", 1), "range": [0, maximum], "affects_signature": True,
                 "update_policy": "restart_required"}
    source_code = TRIGGER_SOURCE if kind == "trigger" else SOURCE
    source_code = source_code.replace("5+p", f"UINT32_C({base})+p").replace(
        "io->items_count<3", f"io->items_count<{maximum}")
    source = create_component(directory, "dynamic_source", {
        "head": port("output", drive_port_no=1),
        "items": port("output", count="param:ports", optional=True,
                      drive_port_no=base, drive_divide=999),
        "tail": port("output", drive_port_no=100),
        "ports": parameter, "mode": scalar("int", 0),
    }, source_code, source=True)
    capture = create_component(directory, "dynamic_capture", {
        "head": port("input", process=True),
        "items": port("input", count="param:ports", optional=True, process=True),
        "tail": port("input", process=True),
        "ports": parameter, "trace": scalar("int", 0), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "total": scalar("float", 0.0),
        "frame": scalar("int", 0), "epoch": scalar("int", 0),
    }, TRIGGER_CAPTURE if kind == "trigger" else CAPTURE)
    registry = Registry()
    for manifest in (source, capture):
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={
            "source": Node("source", "orpheus.test.dynamic_source", params={"ports": count}),
            "capture": Node("capture", "orpheus.test.dynamic_capture", params={"ports": count}),
        },
        connections=[
            *[Connection(PortRef("source", f"items{i}"), PortRef("capture", f"items{i}"))
              for i in range(count)],
            Connection(PortRef("source", "tail"), PortRef("capture", "tail")),
        ],
    ))
    return count, base, compile_delivery_backends(directory, registry, project, [source, capture])


def run_backend(backends, executable, mode=0):
    directory, plan_path, *_ = backends
    arguments = [str(plan_path), str(directory / "build" / "components")] if executable == "dynamic" else []
    suffix = ".exe" if os.name == "nt" else ""
    result = subprocess.run(
        [str(directory / "build" / f"{executable}{suffix}"), *arguments, str(mode)],
        capture_output=True, text=True, encoding="utf-8")
    rows = [[float(value) for value in line.split()] for line in result.stdout.splitlines()]
    return result, rows


def test_dynamic_ports_match_independent_oracle(dynamic_backends):
    count, _, backends = dynamic_backends
    trace = int(f"{count}91") if count else 9
    calls = 3 if count else 1
    total = (count + 11) * 8 if count else 80
    expected = [
        [0, 0, trace, calls, 1 if count else 10, total, 0, 1],
        [1, 0, int(f"{trace}{trace}"), calls * 2, 1 if count else 10, total * 2, 8, 1],
    ]
    for executable in ("dynamic", "generated_backend"):
        result, rows = run_backend(backends, executable)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rows == expected


def test_dynamic_plan_numbers_and_actual_input_slots(dynamic_backends):
    count, base, (_, _, plan, _, _) = dynamic_backends
    assert plan.node_configs["source"]["output_drive_ports"] == {
        "head": 1, **{f"items{i}": base + i for i in range(count)}, "tail": 100}
    assert plan.node_configs["capture"]["input_ports"] == [
        "head", *[f"items{i}" for i in range(count)], "tail"]
    assert [(edge["output_port"], edge["input_port"], edge["every"])
            for edge in plan.call_graph["source"]] == [
        *[(base + i, 1 + i, 1) for i in range(count)], (100, count + 1, 1)]
    if plan.node_configs["source"]["trigger_output_ports"]:
        assert plan.buffers == {}


def test_inactive_or_out_of_range_output_fails_without_calling_capture(dynamic_backends):
    _, _, backends = dynamic_backends
    for executable in ("dynamic", "generated_backend"):
        result, rows = run_backend(backends, executable, mode=4)
        assert result.returncode == 4, result.stdout + result.stderr
        assert rows == [[0, -1, 0, 0, 0, 0, 0, 0], [1, -1, 0, 0, 0, 0, 0, 0]]


@pytest.mark.parametrize("value", [-1, 4, True, 1.5, "2", None])
def test_dynamic_count_rejects_non_integer_or_outside_capacity(dynamic_backends, value):
    _, _, (_, _, _, project, registry) = dynamic_backends
    project = copy.deepcopy(project)
    project.graph.nodes["source"].params["ports"] = value
    with pytest.raises(CompileError, match="数量"):
        GraphCompiler(registry).compile(project)


def test_drive_group_cannot_include_reserved_uint32_max():
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition({
            "id": "orpheus.test.reserved_drive", "name": "保留出口编号", "abi_version": 7,
            "clock_source": True,
            "interfaces": {
                "out": {"direction": "output", "type": "trigger", "count": 2,
                        "drive_port_no": 2**32 - 2},
            },
        })
