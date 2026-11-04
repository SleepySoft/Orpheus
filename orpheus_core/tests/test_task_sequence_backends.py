"""直接构造执行计划，验证动态/生成后端的可选 Task 顺序语义。"""

from __future__ import annotations

import json
import shutil
import subprocess
import uuid
from dataclasses import asdict
from pathlib import Path

import pytest

from orpheus_core.builder import run_cmake_with_msvc_env
from orpheus_core.compiler import ExecutionPlan
from orpheus_core.generator import CodeGenerator
from orpheus_core.registry import ComponentInfo, Registry

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = "orpheus.test.sequence"

HEADER = r"""
#ifndef SEQUENCE_H
#define SEQUENCE_H
#include "orpheus_abi.h"
typedef struct {
    int32_t value, other, observed, calls, bulk_seen, values[2];
    int32_t frame_index, frame_count, timestamp_frames;
} SequenceState;
#endif
"""

SOURCE = r"""
#include "sequence.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[] = {
    {.id="value", .type=ORPHEUS_VALUE_INT},
    {.id="other", .type=ORPHEUS_VALUE_INT}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.test.sequence", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .params=params, .param_count=2, .state_size=sizeof(SequenceState), .alignment=8
};
static const OrpheusComponentDescriptor* describe(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    *state = config->state_block ? config->state_block : calloc(1, sizeof(SequenceState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int prepare(void* state, const OrpheusConfig* config) {
    SequenceState* s = state;
    memset(s, 0, sizeof(*s)); s->value=2; s->other=3; s->values[0]=4;
    for (uint32_t i=0; i<config->param_count; ++i) {
        if (!strcmp(config->param_ids[i], "value")) s->value=config->param_values[i].value.i32;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* context) {
    SequenceState* s=state; (void)context;
    s->observed=s->value*100+s->other; s->bulk_seen=s->values[0];
    s->frame_index=(int32_t)context->frame_index;
    s->frame_count=(int32_t)context->frame_count;
    s->timestamp_frames=(int32_t)(context->timestamp*context->sample_rate+0.5);
    ++s->value; ++s->calls; ++s->values[0]; return ORPHEUS_OK;
}
static int get(void* state, const char* key, OrpheusValue* out) {
    SequenceState* s=state; out->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"value")) out->value.i32=s->value;
    else if (!strcmp(key,"other")) out->value.i32=s->other;
    else if (!strcmp(key,"observed")) out->value.i32=s->observed;
    else if (!strcmp(key,"calls")) out->value.i32=s->calls;
    else if (!strcmp(key,"bulk_seen")) out->value.i32=s->bulk_seen;
    else if (!strcmp(key,"frame_index")) out->value.i32=s->frame_index;
    else if (!strcmp(key,"frame_count")) out->value.i32=s->frame_count;
    else if (!strcmp(key,"timestamp_frames")) out->value.i32=s->timestamp_frames;
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}
static int set(void* state, const char* key, const OrpheusValue* value) {
    SequenceState* s=state;
    if (value->type!=ORPHEUS_VALUE_INT || value->value.i32==42) return ORPHEUS_ERR_INVALID_ARG;
    if (!strcmp(key,"value")) s->value=value->value.i32;
    else if (!strcmp(key,"other")) s->other=value->value.i32;
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}
static int slots(void* state, const OrpheusRegistry* registry) {
    SequenceState* s=state;
    ORPHEUS_REG_SLOT(registry,s,value,ORPHEUS_SLOT_SETTING,"value","数值",ORPHEUS_VALUE_INT);
    ORPHEUS_REG_SLOT(registry,s,other,ORPHEUS_SLOT_SETTING,"other","其它值",ORPHEUS_VALUE_INT);
    ORPHEUS_REG_SLOT(registry,s,observed,ORPHEUS_SLOT_PROBE,"observed","处理快照",ORPHEUS_VALUE_INT);
    ORPHEUS_REG_SLOT(registry,s,calls,ORPHEUS_SLOT_PROBE,"calls","调用次数",ORPHEUS_VALUE_INT);
    ORPHEUS_REG_SLOT(registry,s,bulk_seen,ORPHEUS_SLOT_PROBE,"bulk_seen","数组快照",ORPHEUS_VALUE_INT);
    ORPHEUS_REG_SLOT(registry,s,frame_index,ORPHEUS_SLOT_PROBE,"frame_index","绝对帧",ORPHEUS_VALUE_INT);
    ORPHEUS_REG_SLOT(registry,s,frame_count,ORPHEUS_SLOT_PROBE,"frame_count","实际帧数",ORPHEUS_VALUE_INT);
    ORPHEUS_REG_SLOT(registry,s,timestamp_frames,ORPHEUS_SLOT_PROBE,"timestamp_frames","时间戳帧",ORPHEUS_VALUE_INT);
    ORPHEUS_REG_ARRAY(registry,s,values,2,ORPHEUS_SLOT_BULK,"values","数组",
        ORPHEUS_VALUE_INT,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED,.min_i32=0,.max_i32=100);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=describe,.create=create,.destroy=destroy,.prepare=prepare,
    .process=process,.get_parameter=get,.set_parameter=set,.register_slots=slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
"""

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#ifdef DYNAMIC
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime runtime;
static int read_value(const char* node,const char* key) {
    OrpheusValue value={}; if(runtime.get_parameter(node,key,&value)) return -999;
    return value.value.i32;
}
static int set_value(const char* node,int value) {
    OrpheusValue v={};v.type=ORPHEUS_VALUE_INT;v.value.i32=value;
    return runtime.set_parameter(node,"value",v);
}
static int write_bulk(const char* node,int value) {
    int32_t data[2]={value,0};return runtime.write_bulk(node,"values",data,2);
}
static int read_bulk(const char* node) {
    int32_t data[2]={};runtime.get_bulk(node,"values",data,2);return data[0];
}
static int step(int task) { return task ? runtime.process_task("main",1) : runtime.process_block(1); }
#else
#include "orpheus_graph.h"
#include "orpheus_control.h"
static int read_value(const char* node,const char* key) {
    OrpheusValue value={0};if(orpheus_control_get_value(node,key,&value)) return -999;
    return value.value.i32;
}
static int set_value(const char* node,int value) {
    OrpheusValue v={0};v.type=ORPHEUS_VALUE_INT;v.value.i32=value;
    return orpheus_control_set_value(node,"value",v);
}
static int write_bulk(const char* node,int value) {
    int32_t data[2]={value,0};return orpheus_control_write_bulk(node,"values",data,2);
}
static int read_bulk(const char* node) {
    int32_t data[2]={0};orpheus_control_get_bulk(node,"values",data,2);return data[0];
}
static int step(int task) { return task ? orpheus_generated_process_task_main(1) : orpheus_generated_process(1); }
#endif
static void context_row(const char* node,int tick) {
    printf("CONTEXT %s %d %d %d %d %d\n",node,tick,read_value(node,"calls"),
        read_value(node,"frame_index"),read_value(node,"frame_count"),
        read_value(node,"timestamp_frames"));
}
int main(int argc,char** argv) {
#ifdef DYNAMIC
    if(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])) return 2;
#else
    if(orpheus_generated_init(48000,1)) return 2;
#endif
    const char* mode=argv[argc-1];
    int task=!strcmp(mode,"task"), rc=0;
    if(!strcmp(mode,"error")) set_value("source", 39);
    if(!strcmp(mode,"bulk_error")) write_bulk("source",99);
    write_bulk("spare",17);
    for(int i=0;i<4;++i) {
#ifndef DYNAMIC
        if(!strcmp(mode,"external") || !strcmp(mode,"underflow") || !strcmp(mode,"overflow")) {
            OrpheusExternalTriggerContext trigger={0};
            trigger.epoch=1; trigger.frames=32; trigger.sequence=(uint64_t)i;
            trigger.frame_index=!strcmp(mode,"underflow") ? 0 :
                !strcmp(mode,"overflow") ? UINT64_MAX : (uint64_t)i*32;
            rc=orpheus_generated_process_task_main_at(32,&trigger);
        } else
#endif
        rc=step(task);
        printf("RESULT %d %d %d %d %d %d %d %d\n",i,rc,
            read_value("target","calls"),read_value("target","value"),
            read_value("target","other"),read_value("target","observed"),
            read_value("target","bulk_seen"),read_bulk("spare"));
        context_row("sparse",i); context_row("aggregate",i);
        if(rc) break;
    }
    return 0;
}
"""


def _plan(mode: str) -> ExecutionPlan:
    plan = ExecutionPlan(abi_version=5, sample_rate=48000, block_size=1, task_id="main")
    plan.flow_mode = mode
    plan.bridge_identity = {"graph_hash": 1, "plan_hash": 2, "id_map_hash": 3}
    plan.nodes = plan.execution_order = ["source", "target", "sparse", "aggregate", "spare"]
    plan.schedule = {"tick": 1}
    for nid in plan.nodes:
        plan.node_configs[nid] = {
            "component": COMPONENT, "task": "spare" if nid == "spare" else "main",
            "params": {"value": 10 if nid == "source" else 2},
            "input_ports": [], "output_ports": [], "frames": 1,
            "period": 4 if nid == "target" else 1,
            "phase": 2 if nid == "target" else 0,
        }
    plan.tasks = [
        {"id": "main", "block_size": 32, "sample_rate": 48000, "trigger_group": "external",
         "nodes": ["source", "target", "sparse", "aggregate"],
         "execution_order": ["source", "target", "sparse", "aggregate"],
         "schedule": {"tick": 1, "periods": {"source": 1, "target": 2},
                      "phases": {"source": 0, "target": 1}}},
        {"id": "spare", "nodes": ["spare"], "execution_order": ["spare"],
         "schedule": {"tick": 1, "periods": {"spare": 1}}},
    ]
    for nid, frames in [("sparse", 32), ("aggregate", 64)]:
        plan.node_configs[nid].update({
            "period": 2, "phase": 1, "frames": frames,
            "frame_step": 64, "frame_origin": 64 - frames,
        })
        plan.tasks[0]["schedule"]["periods"][nid] = 2
        plan.tasks[0]["schedule"]["phases"][nid] = 1
    plan.modules = [{"path": "", "id": 0, "leaves": [
        {"node": nid, "slot": i} for i, nid in enumerate(plan.nodes)
    ]}]
    plan.id_map = [
        {"id": 0x01000000 + i, "node": nid, "key": "values", "kind": "TUNE",
         "form": "BULK", "type": "int", "count": 2, "double_bank": True}
        for i, nid in enumerate(plan.nodes, 1)
    ]
    for nid in plan.nodes:
        for key in ["value", "other", "observed", "calls", "bulk_seen",
                    "frame_index", "frame_count", "timestamp_frames"]:
            plan.id_map.append({
                "id": 0x01000100 + len(plan.id_map), "node": nid, "key": key,
                "kind": "TUNE" if key in {"value", "other"} else "PROBE",
                "form": "SCALAR", "type": "int", "count": 1,
            })
    for source, source_key, target_key, bulk in [
        ("source", "value", "value", False),
        ("target", "value", "other", False),
        ("source", "values", "values", True),
    ]:
        plan.control_links.append({
            "src_node": source, "src_param": source_key, "src_slot": source_key,
            "dst_node": "target", "dst_param": target_key, "dst_slot": target_key,
            "type": "int", "transport": "bulk" if bulk else "scalar",
            "shape": [2] if bulk else [], "count": 2 if bulk else 1,
            "delivery": "before_call",
        })
    return plan


@pytest.fixture(scope="module", params=["task_sequence", "dataflow"])
def backend_pair(request):
    if not (ROOT / "build" / "orpheus_abi" / "liborpheus_bridge_endpoint.a").exists():
        pytest.skip("需要已有 MinGW bridge 静态库")
    directory = ROOT / "workspace" / f"backend_sequence_{uuid.uuid4().hex[:8]}"
    directory.mkdir(parents=True)
    try:
        component_dir = directory / "component"
        (component_dir / "src").mkdir(parents=True)
        (component_dir / "include").mkdir()
        (component_dir / "src" / "sequence.c").write_text(SOURCE, encoding="utf-8")
        (component_dir / "include" / "sequence.h").write_text(HEADER, encoding="utf-8")
        registry = Registry()
        registry._components[COMPONENT] = ComponentInfo(
            id=COMPONENT, version="1.0.0", abi_version=5, package_type="source",
            manifest_path=component_dir / "component.yaml", root_dir=component_dir,
            manifest={"id": COMPONENT, "sources": ["src/sequence.c"], "headers": ["include/sequence.h"],
                      "state_type": "SequenceState",
                      "parameters": [{"id": "value", "type": "int"}]},
        )
        plan = _plan(request.param)
        document = asdict(plan)
        document["flow_mode"] = plan.flow_mode
        plan_path = directory / "plan.json"
        plan_path.write_text(json.dumps(document), encoding="utf-8")
        generated = directory / "generated"
        CodeGenerator(registry, ROOT).generate(plan, generated)
        (directory / "harness.c").write_text(HARNESS, encoding="utf-8")
        (directory / "harness.cpp").write_text(HARNESS, encoding="utf-8")
        cmake = f"""
cmake_minimum_required(VERSION 3.16)
project(sequence_backend C CXX)
include_directories("${{CMAKE_CURRENT_SOURCE_DIR}}/generated/include")
add_subdirectory(generated)
add_library(sequence SHARED component/src/sequence.c)
target_include_directories(sequence PRIVATE "{ROOT.as_posix()}/orpheus_abi/include" component/include)
set_target_properties(sequence PROPERTIES PREFIX "lib" OUTPUT_NAME "orpheus_test_sequence")
add_executable(dynamic harness.cpp)
target_compile_definitions(dynamic PRIVATE DYNAMIC)
target_include_directories(dynamic PRIVATE "{ROOT.as_posix()}/orpheus_runtime/include" "{ROOT.as_posix()}/orpheus_abi/include")
add_library(sequence_runtime STATIC
    "{ROOT.as_posix()}/orpheus_runtime/src/runtime.cpp"
    "{ROOT.as_posix()}/orpheus_runtime/src/plan.cpp"
    "{ROOT.as_posix()}/orpheus_runtime/src/loader.cpp"
    "{ROOT.as_posix()}/orpheus_runtime/src/runtime_bridge.cpp")
target_include_directories(sequence_runtime PUBLIC "{ROOT.as_posix()}/orpheus_runtime/include" "{ROOT.as_posix()}/orpheus_abi/include" "{ROOT.as_posix()}/third_party")
target_link_libraries(sequence_runtime PUBLIC "{ROOT.as_posix()}/build/orpheus_abi/liborpheus_bridge_endpoint.a" ws2_32)
set_target_properties(sequence_runtime PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
target_link_libraries(dynamic PRIVATE sequence_runtime)
add_executable(generated_backend harness.c)
target_link_libraries(generated_backend PRIVATE orpheus_graph)
"""
        (directory / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
        args = ["cmake", "-S", str(directory), "-B", str(directory / "build"), "-G", "Ninja"]
        for line in (ROOT / "build" / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
            if line.startswith(("CMAKE_C_COMPILER:", "CMAKE_CXX_COMPILER:")):
                args.append(f"-D{line.split(':', 1)[0]}={line.split('=', 1)[1]}")
        for command in [args, ["cmake", "--build", str(directory / "build"),
                               "--target", "dynamic", "generated_backend", "sequence"]]:
            result = run_cmake_with_msvc_env(command, cwd=directory, build_dir=ROOT / "build")
            assert result.returncode == 0, result.stdout + result.stderr
        yield request.param, directory, plan_path
    finally:
        shutil.rmtree(directory, ignore_errors=True)


@pytest.mark.parametrize("entry", ["global", "task", "error", "bulk_error"])
def test_sequence_dynamic_generated_match(backend_pair, entry):
    mode, directory, plan_path = backend_pair
    results = []
    for executable, arguments in [
        ("dynamic", [str(plan_path), str(directory / "build")]),
        ("generated_backend", []),
    ]:
        process = subprocess.run(
            [str(directory / "build" / f"{executable}.exe"), *arguments, entry],
            capture_output=True, text=True, encoding="utf-8",
        )
        assert process.returncode == 0, process.stdout + process.stderr
        results.append([
            [int(part) for part in line.split()[1:]]
            for line in process.stdout.splitlines() if line.startswith("RESULT ")
        ])
    assert results[0] == results[1], results
    rows = results[0]
    if mode == "task_sequence":
        if entry in {"error", "bulk_error"}:
            assert rows[-1][1] != 0
            assert all(row[2] == 0 for row in rows)
        else:
            firing = 1 if entry == "task" else 2
            assert all(row[2:5] == [0, 2, 3] for row in rows[:firing])
            assert rows[firing][2] == 1
            assert rows[firing][5] == (11 + firing) * 100 + 2
            assert rows[firing][6] == 5 + firing
            if entry == "task":
                assert all(row[7] == 4 for row in rows)
    else:
        assert rows[0][3:5] == ([40, 2] if entry == "error" else [11, 2])
        assert rows[0][7] >= 17
        if entry in {"global", "task"}:
            assert [row[2] for row in rows] == (
                [0, 1, 1, 2] if entry == "task" else [0, 0, 0, 1]
            )


@pytest.mark.parametrize("entry", ["global", "task", "external"])
def test_sequence_frame_origin_context(backend_pair, entry):
    mode, directory, plan_path = backend_pair
    results = []
    executables = ["generated_backend"] if entry == "external" else ["dynamic", "generated_backend"]
    for executable in executables:
        arguments = [str(plan_path), str(directory / "build")] if executable == "dynamic" else []
        process = subprocess.run(
            [str(directory / "build" / f"{executable}.exe"), *arguments, entry],
            capture_output=True, text=True, encoding="utf-8",
        )
        assert process.returncode == 0, process.stdout + process.stderr
        contexts = {}
        for line in process.stdout.splitlines():
            if line.startswith("CONTEXT "):
                _, node, *values = line.split()
                contexts.setdefault(node, []).append([int(value) for value in values])
        results.append(contexts)
        for node, frames in [("sparse", 32), ("aggregate", 64)]:
            origin = 64 - frames if mode == "task_sequence" else 0
            if entry == "external" and mode == "dataflow":
                origin = 32
            assert contexts[node][1] == [1, 1, origin, frames, origin]
            assert contexts[node][3] == [3, 2, 64 + origin, frames, 64 + origin]
    if len(results) == 2:
        assert results[0] == results[1]


@pytest.mark.parametrize("entry", ["underflow", "overflow"])
def test_sequence_external_frame_origin_rejects_wrap(backend_pair, entry):
    mode, directory, _ = backend_pair
    if mode != "task_sequence":
        pytest.skip("旧模式外部触发保留原始帧起点")
    process = subprocess.run(
        [str(directory / "build" / "generated_backend.exe"), entry],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert process.returncode == 0, process.stdout + process.stderr
    rows = [
        [int(value) for value in line.split()[1:]]
        for line in process.stdout.splitlines() if line.startswith("RESULT ")
    ]
    assert rows[-1][1] != 0
    aggregate = [
        line.split()[3:] for line in process.stdout.splitlines()
        if line.startswith("CONTEXT aggregate ")
    ]
    assert all(int(values[0]) == 0 for values in aggregate)
