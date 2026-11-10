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
from orpheus_core.compiler import GraphCompiler
from orpheus_core.component_codegen import generate_component
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project, ProjectLoader
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

HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#ifdef DYNAMIC
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime runtime;
static int get(const char* key) {
    OrpheusValue v={}; if(runtime.get_parameter("capture",key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? (int)v.value.f32 : v.value.i32;
}
#else
#include "orpheus_graph.h"
#include "orpheus_control.h"
static int get(const char* key) {
    OrpheusValue v={0}; if(orpheus_control_get_value("capture",key,&v)) return -999;
    return v.type==ORPHEUS_VALUE_FLOAT ? (int)v.value.f32 : v.value.i32;
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
        printf("%d %d %d %d %d %d %d %d\n",i,rc,get("trace"),get("calls"),
               get("last"),get("total"),get("frame"),get("epoch"));
    }
    return rc ? 4 : 0;
}
'''


@pytest.fixture(scope="module", params=[False, True, "reordered"],
                ids=["direct", "real-linear-gain", "reordered-interfaces"])
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
    source = create_component(directory, "interface_source", source_interfaces, SOURCE, source=True)
    capture = create_component(directory, "interface_capture", capture_interfaces, CAPTURE)
    registry = Registry()
    registry.add_manifest(source)
    registry.add_manifest(capture)
    manifests = [source, capture]
    if request.param:
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
    if request.param:
        project.graph.nodes["gain"] = Node(
            "gain", "orpheus.builtin.linear_gain", params={"gain": 2.0, "channels": 1})
        project.graph.connections[0] = Connection(PortRef("source", "a"), PortRef("gain", "in"))
        project.graph.connections.append(Connection(PortRef("gain", "out"), PortRef("capture", "a")))
    plan = GraphCompiler(registry).compile(project)
    plan_path = directory / "plan.json"
    plan_path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    CodeGenerator(registry, ROOT).generate(plan, directory / "generated")
    (directory / "harness.c").write_text(HARNESS, encoding="utf-8")
    (directory / "harness.cpp").write_text(HARNESS, encoding="utf-8")
    runtime_lib = next(path for path in (ROOT / "build" / "orpheus_runtime").glob("*orpheus_runtime_lib.*")
                       if path.suffix in {".a", ".lib"})
    bridge_lib = next(path for path in (ROOT / "build" / "orpheus_abi").glob("*orpheus_bridge_endpoint.*")
                      if path.suffix in {".a", ".lib"})
    cmake = [
        "cmake_minimum_required(VERSION 3.16)", "project(delivery_backends C CXX)",
        "set(CMAKE_C_STANDARD 11)", "set(CMAKE_CXX_STANDARD 11)",
        "find_package(Threads REQUIRED)",
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
            'LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components")',
        ])
    (directory / "CMakeLists.txt").write_text("\n".join(cmake), encoding="utf-8")
    build = directory / "build"
    configure = ["cmake", "-S", str(directory), "-B", str(build), "-G", "Ninja"]
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
    directory, plan_path, _, project, _ = delivery_backends
    expected = copy.deepcopy(expected)
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
    directory, plan_path, *_ = delivery_backends
    for executable in ("dynamic", "generated_backend"):
        arguments = [str(plan_path), str(directory / "build" / "components")] if executable == "dynamic" else []
        suffix = ".exe" if os.name == "nt" else ""
        result = subprocess.run([str(directory / "build" / f"{executable}{suffix}"), *arguments, "3"],
                                capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 4, result.stdout + result.stderr
        assert [[int(value) for value in line.split()] for line in result.stdout.splitlines()] == [
            [0, -2, 2, 1, 2, 16, 0, 1], [1, -2, 22, 2, 2, 32, 8, 1]]


def test_plan_preserves_distinct_interfaces_and_ignores_hint(delivery_backends):
    _, _, plan, project, registry = delivery_backends
    source_edges = plan.call_graph["source"]
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
