"""标量计算、表源和插值的真实C契约，不通过音频或块末控制链模拟。"""
from pathlib import Path
import re
import subprocess

from orpheus_core.project import Connection, DriveConnection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import compile_delivery_backends, create_component, scalar
from test_table_interfaces import CLOCK, four_entry_harness

ROOT = Path(__file__).resolve().parents[2]


def test_scalar_primitives_native_contract(tmp_path):
    match = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$",
                      (ROOT / "build/CMakeCache.txt").read_text(encoding="utf-8"), re.M)
    assert match
    compiler = match[1]
    includes = ["-I" + str(ROOT / "orpheus_abi/include")]
    objects = []
    for name, entry in (("scalar_math", "scalar_api"), ("bulk_constant", "constant_api"),
                        ("bulk_lookup", "lookup_api"), ("scalar_memory", "memory_api")):
        directory = ROOT / "components/orpheus/builtin" / name
        sources = [directory / "generated" / "component_shell.c", directory / "user" / "algorithm.c"]
        for index, source in enumerate(sources):
            target = tmp_path / f"{name}_{index}.o"
            command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-DORPHEUS_BUILDING_COMPONENT", "-DORPHEUS_ENTRY_NAME=" + entry,
                       "-I" + str(directory / "generated"), "-I" + str(directory / "user"),
                       *includes, "-c", str(source), "-o", str(target)]
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
            assert result.returncode == 0, result.stdout + result.stderr
            objects.append(str(target))
    executable = tmp_path / "scalar_primitives.exe"
    result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *includes,
                             str(ROOT / "tests/scalar_primitives_smoke.c"), *objects, "-lm",
                             "-o", str(executable)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


TABLE_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_table_source_algorithm(orpheus_test_table_source_State* s,
    orpheus_test_table_source_IO* io,const OrpheusProcessContext* ctx) {
    int rc=orpheus_test_table_source_emit_tables(ctx,&io->tables);
    if(rc) return rc;
    for(int phase=0;phase<2;phase++) {
        io->out.samples[0]=(float)((s->ticks*2+phase)%9-4)*0.75f;
        rc=orpheus_test_table_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    s->ticks++;
    return ORPHEUS_OK;
}
'''

TABLE_CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_table_capture_algorithm(orpheus_test_table_capture_State* s,
    orpheus_test_table_capture_IO* io,const OrpheusProcessContext* ctx) {
    float raw=(float)(s->calls%9-4)*0.75f;
    float expected=raw < -2 || raw > 2 ? 0 : raw<=0 ? raw*2 : raw*3;
    if(ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/2*8)
        || ctx->trigger_input!=ORPHEUS_TEST_TABLE_CAPTURE_INPUT_RESULT
        || memcmp(io->raw.samples,&raw,4) || memcmp(io->result.samples,&expected,4))
        return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=io->result.samples[0];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


def test_table_primitives_explicit_chain_four_entries(tmp_path):
    source = create_component(tmp_path, "table_source", {
        "tables": {"direction": "output", "type": "trigger", "drive_port_no": 1},
        "out": {"direction": "output", "type": "bulk", "sample_format": "f32",
                "channels": 1, "block_size": 1, "drive_port_no": 2},
        "ticks": scalar("int", 0),
    }, TABLE_SOURCE, source=True)
    port = {"direction": "input", "type": "bulk", "sample_format": "f32", "channels": 1, "block_size": 1}
    capture = create_component(tmp_path, "table_capture", {
        "raw": dict(port), "result": {**port, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, TABLE_CAPTURE)
    manifests = [source, capture, *[
        ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
        for name in ("bulk_constant", "bulk_lookup")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    nodes = {
        "source": Node("source", "orpheus.test.table_source"),
        "axis": Node("axis", "orpheus.builtin.bulk_constant", params={"elements": 3, "values": "-2,0,2"}),
        "values": Node("values", "orpheus.builtin.bulk_constant", params={"elements": 3, "values": "-4,0,6"}),
        "lookup": Node("lookup", "orpheus.builtin.bulk_lookup", params={"points": 3}),
        "capture": Node("capture", "orpheus.test.table_capture"),
    }
    pairs = [("source:out", "lookup:x"),
             ("axis:out", "lookup:axis"), ("values:out", "lookup:values"),
             ("source:out", "capture:raw"), ("lookup:out", "capture:result")]
    project = Project(block_size=8,
        flow_mode="task_sequence", graph=Graph(nodes=nodes, connections=[
            Connection(PortRef.parse(a), PortRef.parse(b)) for a, b in pairs]),
        drive_connections=[DriveConnection(PortRef("source", "tables"), PortRef(name, "tick"))
                           for name in ("axis", "values")])
    harness = four_entry_harness().replace("tick<2", "tick<24")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert case[2].entry_nodes == ["source"]
    expected = []
    for tick in range(24):
        x = ((tick * 2 + 1) % 9 - 4) * .75
        expected.append([tick, 2 * (tick + 1), 0 if x < -2 or x > 2 else x * (2 if x <= 0 else 3), tick * 8])
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            result, rows = run_backend(case, backend, mode=mode)
            assert result.returncode == 0, result.stdout + result.stderr
            assert rows == expected


def test_scalar_memory_feedback_four_entries(tmp_path):
    source = create_component(tmp_path, "constant_clock", {
        "out": {"direction": "output", "type": "trigger", "drive_port_no": 1},
    }, CLOCK, source=True)
    port = {"direction": "input", "type": "bulk", "sample_format": "f32", "channels": 1, "block_size": 1}
    capture = create_component(tmp_path, "feedback_capture", {
        "previous": {**port, "timing": "latest"}, "result": {**port, "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, r'''
#include "component_state.h"
int orpheus_test_feedback_capture_algorithm(orpheus_test_feedback_capture_State* s,
    orpheus_test_feedback_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(ctx->frame_count!=8 || ctx->valid_frames!=8 || ctx->epoch!=1
        || ctx->frame_index!=(uint64_t)(s->calls/2*8)
        || io->previous.samples[0]!=3.0f+2*s->calls || io->result.samples[0]!=5.0f+2*s->calls)
        return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->last=io->result.samples[0];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
''')
    manifests = [source, capture, *[
        ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
        for name in ("scalar_memory", "scalar_math")]]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    nodes = {
        "source": Node("source", "orpheus.test.constant_clock"),
        "memory": Node("memory", "orpheus.builtin.scalar_memory",
                       params={"initial_value": 3.0, "write_after_call": True}),
        "sum": Node("sum", "orpheus.builtin.scalar_math", params={"operation": 6, "b_default": 2.0}),
        "capture": Node("capture", "orpheus.test.feedback_capture"),
    }
    pairs = [("memory:out", "sum:a"), ("sum:out", "memory:in"),
             ("memory:out", "capture:previous"), ("sum:out", "capture:result")]
    project = Project(block_size=8,
        flow_mode="task_sequence", graph=Graph(nodes=nodes, connections=[
            Connection(PortRef.parse(a), PortRef.parse(b)) for a, b in pairs]),
        drive_connections=[DriveConnection(PortRef("source", "out"), PortRef("memory", "tick"))])
    case = compile_delivery_backends(tmp_path, registry, project, manifests,
                                     four_entry_harness().replace("tick<2", "tick<24"))
    assert case[2].entry_nodes == ["source"]
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            result, rows = run_backend(case, backend, mode=mode)
            assert result.returncode == 0, (backend, mode, result.stdout, result.stderr)
            assert rows == [[i, 2 * (i + 1), 7 + 4 * i, 8 * i] for i in range(24)]
