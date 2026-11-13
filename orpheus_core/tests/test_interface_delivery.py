"""接口身份、交付时间和独立生成组件的双路径数值回归。"""

import copy
import json
import os
import subprocess
from dataclasses import asdict
from pathlib import Path

import pytest
import yaml

from orpheus_core.builder import BuildError, ComponentBuilder, run_cmake_with_msvc_env
from orpheus_core.component_definition import ComponentDefinitionError, normalize_component_definition
from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.component_codegen import generate_component
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project, ProjectLoader
from orpheus_core.registry import Registry
from orpheus_core.task_flow import resolve_drive_connections

ROOT = Path(__file__).resolve().parents[2]


def audio(direction, **fields):
    return {"direction": direction, "type": "audio", "sample_format": "f32",
            "channels": 1, **fields}


def scalar(kind, default):
    return {"type": kind, "default": default, "readback": True}


def create_component(directory, name, interfaces, algorithm, source=False):
    package = directory / name
    (package / "user").mkdir(parents=True)
    manifest = package / "component.yaml"
    manifest.write_text(yaml.safe_dump({
        "id": f"orpheus.test.{name}", "name": name, "version": "1.0.0",
        "abi_version": 7, "clock_source": source, "interfaces": interfaces,
    }, sort_keys=False), encoding="utf-8")
    (package / "user" / "algorithm.c").write_text(algorithm, encoding="utf-8")
    generate_component(manifest)
    return manifest


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_interface_source_algorithm(orpheus_test_interface_source_State* s,
    orpheus_test_interface_source_IO* io, const OrpheusProcessContext* ctx) {
    for (uint32_t i=0;i<ctx->frame_count;i++) {
        io->a.samples[i]=1; io->b.samples[i]=2;
    }
    int rc=orpheus_test_interface_source_emit_b(ctx,&io->b);
    if(rc || s->mode==1) return rc;
    rc=orpheus_test_interface_source_emit_a(ctx,&io->a);
    if(rc || s->mode!=2) return rc;
    for(uint32_t i=0;i<ctx->frame_count;i++) io->b.samples[i]=3;
    io->b.delivery.frame_index=ctx->frame_index+5;
    return orpheus_test_interface_source_emit_b(ctx,&io->b);
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_interface_capture_algorithm(orpheus_test_interface_capture_State* s,
    orpheus_test_interface_capture_IO* io, const OrpheusProcessContext* ctx) {
    if(ctx->trigger_input!=ORPHEUS_TEST_INTERFACE_CAPTURE_INPUT_A
        && ctx->trigger_input!=ORPHEUS_TEST_INTERFACE_CAPTURE_INPUT_B) return ORPHEUS_ERR_INVALID_ARG;
    const int is_a=ctx->trigger_input==ORPHEUS_TEST_INTERFACE_CAPTURE_INPUT_A;
    if(is_a && s->fail) return ORPHEUS_ERR_UNSUPPORTED;
    const float* values=is_a ? io->a.samples : io->b.samples;
    s->trace=s->trace*10+(is_a ? 1 : 2);
    s->calls++;
    s->last=values[0];
    for(uint32_t i=0;i<ctx->frame_count;i++) s->total+=values[i];
    s->frame=(int32_t)ctx->frame_index;
    s->epoch=(int32_t)ctx->epoch;
    return ORPHEUS_OK;
}
'''

TRIGGER_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_interface_source_algorithm(orpheus_test_interface_source_State* s,
    orpheus_test_interface_source_IO* io, const OrpheusProcessContext* ctx) {
    int rc=orpheus_test_interface_source_emit_b(ctx,&io->b);
    if(rc || s->mode==1) return rc;
    rc=orpheus_test_interface_source_emit_a(ctx,&io->a);
    if(rc || s->mode!=2) return rc;
    io->b.delivery.frame_index=ctx->frame_index+5;
    return orpheus_test_interface_source_emit_b(ctx,&io->b);
}
'''

TRIGGER_CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_interface_capture_algorithm(orpheus_test_interface_capture_State* s,
    orpheus_test_interface_capture_IO* io, const OrpheusProcessContext* ctx) {
    if(!ctx->delivery || ctx->delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    const int is_a=ctx->trigger_input==ORPHEUS_TEST_INTERFACE_CAPTURE_INPUT_A;
    if(ctx->trigger_input!=ORPHEUS_TEST_INTERFACE_CAPTURE_INPUT_A
        && ctx->trigger_input!=ORPHEUS_TEST_INTERFACE_CAPTURE_INPUT_B) return ORPHEUS_ERR_INVALID_ARG;
    if((is_a ? io->a.delivery : io->b.delivery)!=ctx->delivery) return ORPHEUS_ERR_INVALID_ARG;
    if(is_a && s->fail) return ORPHEUS_ERR_UNSUPPORTED;
    s->last=is_a ? 1 : 2;
    s->trace=s->trace*10+(int32_t)s->last;
    s->total+=s->last;
    s->calls++;
    s->frame=(int32_t)ctx->frame_index;
    s->epoch=(int32_t)ctx->epoch;
    return ORPHEUS_OK;
}
'''

HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#ifdef DYNAMIC
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime runtime;
static double get(const char* key) {
    OrpheusValue v={}; if(runtime.get_parameter("capture",key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? (double)v.value.f32 : v.value.i32;
}
#else
#include "orpheus_graph.h"
#include "orpheus_control.h"
static double get(const char* key) {
    OrpheusValue v={0}; if(orpheus_control_get_value("capture",key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? (double)v.value.f32 : v.value.i32;
}
#endif
int main(int argc,char** argv) {
    int rc=0,mode=atoi(argv[argc-1]);
    OrpheusValue value={ORPHEUS_VALUE_INT};value.value.i32=mode;
#ifdef DYNAMIC
    if(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])) return 2;
    if(runtime.set_parameter("source","mode",value)) return 3;
#else
    if(orpheus_generated_init(48000,8)) return 2;
    if(orpheus_control_set_value("source","mode",value)) return 3;
#endif
    if(mode==3) {
        value.type=ORPHEUS_VALUE_BOOL;value.value.b=true;
#ifdef DYNAMIC
        if(runtime.set_parameter("capture","fail",value)) return 3;
#else
        if(orpheus_control_set_value("capture","fail",value)) return 3;
#endif
    }
    for(int i=0;i<2;i++) {
#ifdef DYNAMIC
        rc=runtime.process_task("task_source",8);
#else
        rc=orpheus_generated_process_task_task_source(8);
#endif
        printf("%d %d %.9g %.9g %.9g %.9g %.9g %.9g\n",i,rc,get("trace"),get("calls"),
               get("last"),get("total"),get("frame"),get("epoch"));
    }
    return rc ? 4 : 0;
}
'''


@pytest.fixture(scope="module", params=[False, True, "reordered", "trigger"],
                ids=["direct", "real-linear-gain", "reordered-interfaces", "pure-trigger"])
def delivery_backends(tmp_path_factory, request):
    directory = tmp_path_factory.mktemp("interface_delivery")
    source_interfaces = {
        "a": audio("output", drive_port_no=1, drive_divide=2),
        "b": audio("output", drive_port_no=2, drive_divide=999),
        "mode": scalar("int", 0),
    }
    capture_interfaces = {
        "a": audio("input", process=True), "b": audio("input", process=True),
        "trace": scalar("int", 0), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "total": scalar("float", 0.0),
        "frame": scalar("int", 0), "epoch": scalar("int", 0),
        "fail": scalar("bool", False),
    }
    if request.param == "reordered":
        source_interfaces = dict(reversed(list(source_interfaces.items())))
        capture_interfaces = dict(reversed(list(capture_interfaces.items())))
    if request.param == "trigger":
        for interfaces in (source_interfaces, capture_interfaces):
            for name in ("a", "b"):
                interfaces[name] = {key: value for key, value in interfaces[name].items()
                                    if key not in {"channels", "sample_format"}}
                interfaces[name]["type"] = "trigger"
    source = create_component(directory, "interface_source", source_interfaces,
                              TRIGGER_SOURCE if request.param == "trigger" else SOURCE, source=True)
    capture = create_component(directory, "interface_capture", capture_interfaces,
                               TRIGGER_CAPTURE if request.param == "trigger" else CAPTURE)
    registry = Registry()
    registry.add_manifest(source)
    registry.add_manifest(capture)
    manifests = [source, capture]
    if request.param in (True, "reordered"):
        gain = ROOT / "components" / "orpheus" / "builtin" / "linear_gain" / "component.yaml"
        registry.add_manifest(gain)
        manifests.append(gain)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={"source": Node("source", "orpheus.test.interface_source"),
               "capture": Node("capture", "orpheus.test.interface_capture")},
        connections=[
            Connection(PortRef("source", "a"), PortRef("capture", "a")),
            Connection(PortRef("source", "b"), PortRef("capture", "b")),
        ],
    ))
    if request.param in (True, "reordered"):
        project.graph.nodes["gain"] = Node(
            "gain", "orpheus.builtin.linear_gain", params={"gain": 2.0, "channels": 1})
        project.graph.connections[0] = Connection(PortRef("source", "a"), PortRef("gain", "in"))
        project.graph.connections.append(Connection(PortRef("gain", "out"), PortRef("capture", "a")))
    return compile_delivery_backends(directory, registry, project, manifests)


def compile_delivery_backends(directory, registry, project, manifests, harness=HARNESS):
    plan = GraphCompiler(registry).compile(project)
    plan_path = directory / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    CodeGenerator(registry, ROOT).generate(plan, directory / "generated")
    (directory / "harness.c").write_text(harness, encoding="utf-8")
    (directory / "harness.cpp").write_text(harness, encoding="utf-8")
    runtime_lib = next(path for path in (ROOT / "build" / "orpheus_runtime").glob("*orpheus_runtime_lib.*")
                       if path.suffix in {".a", ".lib"})
    bridge_lib = next(path for path in (ROOT / "build" / "orpheus_abi").glob("*orpheus_bridge_endpoint.*")
                      if path.suffix in {".a", ".lib"})
    cmake = [
        "cmake_minimum_required(VERSION 3.16)", "project(delivery_backends C CXX)",
        "set(CMAKE_C_STANDARD 11)", "set(CMAKE_CXX_STANDARD 11)",
        "find_package(Threads REQUIRED)",
        # MSVC 必须按 UTF-8 读取组件源码（中文注释/字符串），与主构建保持一致
        "if(MSVC)", "add_compile_options(/utf-8 /EHsc)", "endif()",
        # 运行时在 Windows 按 lib<name>.dll 加载组件；MSVC 默认不加 lib 前缀，统一之
        "if(WIN32)", "set(CMAKE_SHARED_LIBRARY_PREFIX lib)", "endif()",
        "add_subdirectory(generated)",
        "add_executable(generated_backend harness.c)",
        "target_link_libraries(generated_backend PRIVATE orpheus_graph)",
        "add_executable(dynamic harness.cpp)",
        "target_compile_definitions(dynamic PRIVATE DYNAMIC)",
        f'target_include_directories(dynamic PRIVATE "{ROOT.as_posix()}/orpheus_runtime/include" '
        f'"{ROOT.as_posix()}/orpheus_abi/include")',
        f'target_link_libraries(dynamic PRIVATE "{runtime_lib.as_posix()}" "{bridge_lib.as_posix()}" Threads::Threads)',
        "if(WIN32)", "target_link_libraries(dynamic PRIVATE ws2_32)", "endif()",
    ]
    for manifest in manifests:
        info = registry.add_manifest(manifest)
        target = info.id.replace(".", "_")
        cmake.extend([
            f"add_library({target}_dynamic SHARED",
            *[f'  "{path.as_posix()}"' for path in registry.source_files(info)], ")",
            f"target_compile_definitions({target}_dynamic PRIVATE ORPHEUS_BUILDING_COMPONENT)",
            f'target_include_directories({target}_dynamic PRIVATE '
            f'"{ROOT.as_posix()}/orpheus_abi/include" '
            + " ".join(f'"{p.as_posix()}"' for p in registry.include_dirs(info)) + ")",
            f"set_target_properties({target}_dynamic PROPERTIES OUTPUT_NAME {target} "
            'RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
            'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components" '
            # MSVC 的 DLL 导入库走 ARCHIVE 输出目录；不设置会落到构建根目录，
            # 与子目录静态库的便捷目标重名冲突（multiple rules generate ...lib）。
            'ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")',
        ])
        if "miniaudio" in info.manifest.get("deps", []):
            cmake.append(f'target_include_directories({target}_dynamic PRIVATE "{ROOT.as_posix()}/third_party")')
    (directory / "CMakeLists.txt").write_text("\n".join(cmake), encoding="utf-8")
    build = directory / "build"
    # 与预构建 runtime/abi 库保持一致：MSVC 下默认 Debug(MDd) 与主构建的 Release(MD) 冲突
    configure = ["cmake", "-S", str(directory), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release"]
    for line in (ROOT / "build" / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(("CMAKE_C_COMPILER:", "CMAKE_CXX_COMPILER:")):
            configure.append(f"-D{line.split(':', 1)[0]}={line.split('=', 1)[1]}")
    for command in (configure, ["cmake", "--build", str(build)]):
        result = run_cmake_with_msvc_env(command, directory, ROOT / "build")
        assert result.returncode == 0, result.stdout + result.stderr
    return directory, plan_path, plan, project, registry


@pytest.mark.parametrize("mode,expected", [
    (0, [[0, 0, 21, 2, 1, 24, 0, 1], [1, 0, 2121, 4, 1, 48, 8, 1]]),
    (1, [[0, 0, 2, 1, 2, 16, 0, 1], [1, 0, 22, 2, 2, 32, 8, 1]]),
    (2, [[0, 0, 212, 3, 3, 48, 5, 1], [1, 0, 212212, 6, 3, 96, 13, 1]]),
])
def test_delivery_dynamic_generated_match_independent_oracle(delivery_backends, mode, expected):
    directory, plan_path, plan, project, _ = delivery_backends
    expected = copy.deepcopy(expected)
    if plan.node_configs["source"].get("trigger_output_ports"):
        expected = {
            0: [[0, 0, 21, 2, 1, 3, 0, 1], [1, 0, 2121, 4, 1, 6, 8, 1]],
            1: [[0, 0, 2, 1, 2, 2, 0, 1], [1, 0, 22, 2, 2, 4, 8, 1]],
            2: [[0, 0, 212, 3, 2, 5, 5, 1], [1, 0, 212212, 6, 2, 10, 13, 1]],
        }[mode]
    if "gain" in project.graph.nodes and mode != 1:
        for i, row in enumerate(expected):
            row[5] += 8 * (i + 1)
            if mode == 0:
                row[4] = 2
    results = []
    for executable in ("dynamic", "generated_backend"):
        arguments = [str(plan_path), str(directory / "build" / "components")] if executable == "dynamic" else []
        suffix = ".exe" if os.name == "nt" else ""
        result = subprocess.run([str(directory / "build" / f"{executable}{suffix}"), *arguments, str(mode)],
                                capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        results.append([[int(value) for value in line.split()] for line in result.stdout.splitlines()])
    assert results[0] == results[1] == expected


def test_delivery_error_propagates_and_next_task_can_run(delivery_backends):
    directory, plan_path, plan, *_ = delivery_backends
    pulse = bool(plan.node_configs["source"].get("trigger_output_ports"))
    for executable in ("dynamic", "generated_backend"):
        arguments = [str(plan_path), str(directory / "build" / "components")] if executable == "dynamic" else []
        suffix = ".exe" if os.name == "nt" else ""
        result = subprocess.run([str(directory / "build" / f"{executable}{suffix}"), *arguments, "3"],
                                capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 4, result.stdout + result.stderr
        assert [[int(value) for value in line.split()] for line in result.stdout.splitlines()] == [
            [0, -2, 2, 1, 2, 2 if pulse else 16, 0, 1],
            [1, -2, 22, 2, 2, 4 if pulse else 32, 8, 1]]


def test_plan_preserves_distinct_interfaces_and_ignores_hint(delivery_backends):
    _, _, plan, project, registry = delivery_backends
    source_edges = plan.call_graph["source"]
    if plan.node_configs["source"].get("trigger_output_ports"):
        assert plan.buffers == {}
    b_slot = plan.node_configs["capture"]["input_ports"].index("b")
    assert [(e["output_port"], e["input_port"]) for e in source_edges] == [(1, 0), (2, b_slot)]
    assert [e["source_interface"] for e in source_edges] == ["a", "b"]
    local = registry.fork()
    info = local.get("orpheus.test.interface_source")
    raw = yaml.safe_load(info.manifest_path.read_text(encoding="utf-8"))
    raw["interfaces"]["a"]["drive_divide"] = 777
    raw["interfaces"]["b"]["drive_divide"] = 123
    info.manifest = normalize_component_definition(raw)
    assert GraphCompiler(local).compile(copy.deepcopy(project)).call_graph == plan.call_graph


def test_real_component_demo_compiles():
    from orpheus_core.server.app import _component_to_dict

    registry = Registry([ROOT / "components"])
    registry.scan()
    project = ProjectLoader(registry).load(ROOT / "examples" / "interface_delivery_demo.yaml")
    plan = GraphCompiler(registry).compile(project)
    assert plan.entry_nodes == ["source"]
    assert plan.call_graph["source"][0]["output_port"] == 1
    assert plan.call_graph["gain"][0]["target_interface"] == "in"
    assert all(_component_to_dict(registry.get(node.component))["declared_interfaces"]
               for node in project.graph.nodes.values())


def test_unmarked_interfaces_do_not_infer_drives(delivery_backends):
    _, _, _, project, registry = delivery_backends
    local = registry.fork()
    source = local.get("orpheus.test.interface_source")
    raw = yaml.safe_load(source.manifest_path.read_text(encoding="utf-8"))
    for interface in ("a", "b"):
        raw["interfaces"][interface].pop("drive_port_no")
        raw["interfaces"][interface].pop("drive_divide")
    if raw["interfaces"]["a"]["type"] == "trigger":
        with pytest.raises(ComponentDefinitionError, match="trigger 输出必须声明 drive_port_no"):
            normalize_component_definition(raw)
        return
    source.manifest = normalize_component_definition(raw)
    assert not [edge for edge in resolve_drive_connections(project, local) if edge.from_ref.node_id == "source"]


def test_build_and_graph_generation_reject_stale_shell_without_output(tmp_path):
    manifest = create_component(tmp_path, "interface_source",
                                {"a": audio("output", drive_port_no=1)}, SOURCE, source=True)
    registry = Registry()
    registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(
        nodes={"source": Node("source", "orpheus.test.interface_source")}, connections=[]))
    plan = GraphCompiler(registry).compile(project)
    (manifest.parent / "generated" / "component_api.h").write_text("stale\n", encoding="utf-8")
    output = tmp_path / "output"
    with pytest.raises(ComponentDefinitionError):
        CodeGenerator(registry, ROOT).generate(plan, output)
    assert not output.exists()
    with pytest.raises(BuildError, match="组件生成物不可用"):
        ComponentBuilder(ROOT, tmp_path / "build", registry).build_component("orpheus.test.interface_source")
    assert not (tmp_path / "build").exists()


PARTIAL_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_interface_source_algorithm(orpheus_test_interface_source_State* s,
    orpheus_test_interface_source_IO* io, const OrpheusProcessContext* ctx) {
    (void)s;
    float values[5]={1,2,3,4,5};
    OrpheusBuffer first={.data=values,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_capacity=2,.frame_count=2,.interleaved=true};
    OrpheusBuffer second={.data=values+2,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_capacity=3,.frame_count=3,.interleaved=true};
    orpheus_test_interface_source_OutputF32 part=io->out;
    part.buffer=&first;part.samples=values;
    part.delivery.buffer=&first;
    int rc=orpheus_test_interface_source_emit_out(ctx,&part);
    if(rc) return rc;
    part.buffer=&second;part.samples=values+2;part.delivery.buffer=&second;
    part.delivery.frame_index+=2;
    return orpheus_test_interface_source_emit_out(ctx,&part);
}
'''

BRIDGE_CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_interface_capture_algorithm(orpheus_test_interface_capture_State* s,
    orpheus_test_interface_capture_IO* io, const OrpheusProcessContext* ctx) {
    if(!io->in.buffer || ctx->frame_count!=8) return ORPHEUS_ERR_INVALID_ARG;
    for(uint32_t i=0;i<ctx->frame_count;i++) s->total+=io->in.samples[i];
    s->calls++;
    return ORPHEUS_OK;
}
'''

CLOCK_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_interface_clock_algorithm(orpheus_test_interface_clock_State* state,
    orpheus_test_interface_clock_IO* io, const OrpheusProcessContext* ctx) {
    (void)state;
    return orpheus_test_interface_clock_emit_out(ctx,&io->out);
}
'''

BRIDGE_HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#ifdef DYNAMIC
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime runtime;
static int get(const char* node,const char* key) {
    OrpheusValue v={};if(runtime.get_parameter(node,key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? (int)v.value.f32 : v.value.i32;
}
static int source(void) { return runtime.process_task("task_source",8); }
static int consume(void) { return runtime.process_task("task_clock",8); }
#else
#include "orpheus_graph.h"
#include "orpheus_control.h"
static int get(const char* node,const char* key) {
    OrpheusValue v={0};if(orpheus_control_get_value(node,key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? (int)v.value.f32 : v.value.i32;
}
static int source(void) { return orpheus_generated_process_task_task_source(8); }
static int consume(void) { return orpheus_generated_process_task_task_clock(8); }
#endif
int main(int argc,char** argv) {
    const int mode=atoi(argv[argc-1]);
#ifdef DYNAMIC
    if(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])) return 2;
#else
    if(orpheus_generated_init(48000,8)) return 2;
#endif
    const char* probes[]={"level_frames","underruns","overruns"};
    for(unsigned i=0;i<3;i++) {
        OrpheusValue value={ORPHEUS_VALUE_INT};value.value.i32=123;
#ifdef DYNAMIC
        if(!runtime.set_parameter("bridge",probes[i],value)) return 5;
#else
        if(!orpheus_control_set_value("bridge",probes[i],value)) return 5;
#endif
        if(get("bridge",probes[i])) return 6;
    }
    for(int i=0;i<3;i++) {
        if(source() || (mode && source())) return 3;
        const int rc=consume();
        printf("%d %d %d %d %d %d %d\n",i,rc,get("capture","total"),get("capture","calls"),
               get("bridge","overruns"),get("bridge","underruns"),get("bridge","level_frames"));
        if(rc) return 4;
    }
    return 0;
}
'''


@pytest.fixture(scope="module")
def partial_bridge_backends(tmp_path_factory):
    directory = tmp_path_factory.mktemp("partial_bridge_delivery")
    source = create_component(directory, "interface_source",
                              {"out": audio("output", drive_port_no=1)}, PARTIAL_SOURCE, source=True)
    clock = create_component(directory, "interface_clock",
                             {"out": {"direction": "output", "type": "trigger", "drive_port_no": 1}},
                             CLOCK_SOURCE, source=True)
    capture = create_component(directory, "interface_capture", {
        "in": audio("input", process=True), "total": scalar("float", 0.0),
        "calls": scalar("int", 0),
    }, BRIDGE_CAPTURE)
    manifests = [source, clock, capture,
                 ROOT / "components" / "orpheus" / "builtin" / "async_bridge" / "component.yaml"]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.interface_source"),
        "clock": Node("clock", "orpheus.test.interface_clock"),
        "bridge": Node("bridge", "orpheus.builtin.async_bridge",
                       params={"channels": 1, "capacity_frames": 8}),
        "capture": Node("capture", "orpheus.test.interface_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("bridge", "in")),
        Connection(PortRef("bridge", "out"), PortRef("capture", "in")),
    ]), drive_connections=[DriveConnection(PortRef("clock", "out"), PortRef("bridge", "tick"))])
    return compile_delivery_backends(directory, registry, project, manifests, BRIDGE_HARNESS)


@pytest.mark.parametrize("mode,per_tick,overruns", [(0, 15, 0), (1, 18, 1)])
def test_temporary_partial_delivery_cross_task_ring(partial_bridge_backends, mode, per_tick, overruns):
    directory, plan_path, *_ = partial_bridge_backends
    expected = [[i, 0, per_tick * (i + 1), i + 1, overruns * (i + 1), i + 1, 0] for i in range(3)]
    for executable in ("dynamic", "generated_backend"):
        arguments = [str(plan_path), str(directory / "build" / "components")] if executable == "dynamic" else []
        suffix = ".exe" if os.name == "nt" else ""
        result = subprocess.run([str(directory / "build" / f"{executable}{suffix}"), *arguments, str(mode)],
                                capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        assert [[int(value) for value in line.split()] for line in result.stdout.splitlines()] == expected


ARRAY_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_interface_source_algorithm(orpheus_test_interface_source_State* state,
    orpheus_test_interface_source_IO* io, const OrpheusProcessContext* ctx) {
    float value=0;
    for(unsigned i=0;i<3;i++)
        value+=state->weights[i]+state->offsets[i]+state->enabled[i];
    for(uint32_t i=0;i<ctx->frame_count;i++) {
        io->a.samples[i]=value;io->b.samples[i]=value*2;
    }
    int rc=orpheus_test_interface_source_emit_b(ctx,&io->b);
    return rc ? rc : orpheus_test_interface_source_emit_a(ctx,&io->a);
}
'''


@pytest.fixture(scope="module", params=[False, True, "single"],
                ids=["array-defaults", "array-configured", "array-single"])
def array_delivery_backends(tmp_path_factory, request):
    directory = tmp_path_factory.mktemp("array_initializer_delivery")
    count = 1 if request.param == "single" else 3
    source = create_component(directory, "interface_source", {
        "a": audio("output", drive_port_no=1), "b": audio("output", drive_port_no=2),
        "mode": scalar("int", 0),
        "weights": {**scalar("float", [0.5, 0.25, 0.125][:count]), "count": count, "range": [-4.0, 4.0]},
        "offsets": {**scalar("int", [1, 2, 3][:count]), "count": count},
        "enabled": {**scalar("bool", [True, False, True][:count]), "count": count},
    }, ARRAY_SOURCE.replace("i<3", f"i<{count}"), source=True)
    capture = create_component(directory, "interface_capture", {
        "a": audio("input", process=True), "b": audio("input", process=True),
        "trace": scalar("int", 0), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "total": scalar("float", 0.0),
        "frame": scalar("int", 0), "epoch": scalar("int", 0), "fail": scalar("bool", False),
    }, CAPTURE)
    registry = Registry()
    for manifest in (source, capture):
        registry.add_manifest(manifest)
    params = {"weights": [1.25, -2.0, 0.5], "offsets": [-1, 3, 7],
              "enabled": [False, True, False]} if request.param is True else {}
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.interface_source", params=params),
        "capture": Node("capture", "orpheus.test.interface_capture"),
    }, connections=[
        Connection(PortRef("source", "a"), PortRef("capture", "a")),
        Connection(PortRef("source", "b"), PortRef("capture", "b")),
    ]))
    result = compile_delivery_backends(directory, registry, project, [source, capture])
    return result, 2.5 if count == 1 else 9.75 if request.param is True else 8.875


def test_fixed_numeric_array_initializers_reach_both_backends(array_delivery_backends):
    (directory, plan_path, *_), value = array_delivery_backends
    expected = [[0, 0, 21, 2, value, value * 24, 0, 1],
                [1, 0, 2121, 4, value, value * 48, 8, 1]]
    for executable in ("dynamic", "generated_backend"):
        arguments = [str(plan_path), str(directory / "build" / "components")] if executable == "dynamic" else []
        suffix = ".exe" if os.name == "nt" else ""
        result = subprocess.run([str(directory / "build" / f"{executable}{suffix}"), *arguments, "0"],
                                capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        assert [[float(value) for value in line.split()] for line in result.stdout.splitlines()] == expected


@pytest.mark.parametrize("parameter,value", [
    ("weights", [1.0, 2.0]),
    ("weights", [1.0, True, 3.0]),
    ("weights", [1.0, float("inf"), 3.0]),
    ("weights", [1.0, 5.0, 3.0]),
    ("offsets", [1, 2.5, 3]),
    ("offsets", [1, 2**40, 3]),
    ("enabled", [True, False, 1]),
])
def test_invalid_array_initializers_fail_compilation(array_delivery_backends, parameter, value):
    (_, _, _, project, registry), _ = array_delivery_backends
    invalid = copy.deepcopy(project)
    invalid.graph.nodes["source"].params[parameter] = value
    with pytest.raises(CompileError, match=f"参数 {parameter} 初始化失败"):
        GraphCompiler(registry).compile(invalid)
