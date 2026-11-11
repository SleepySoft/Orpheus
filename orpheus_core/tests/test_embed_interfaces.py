"""嵌入式边界只使用公开宿主端口，动态/生成与全图/Task入口一致。"""

import re
import struct
import subprocess

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_generated_double_bank import _build
from test_host_audio import ROOT, execute_host_graph


@pytest.mark.parametrize("component", ["embed_in", "embed_out"])
def test_embed_native_contract(tmp_path, component):
    match = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$",
                     (ROOT / "build" / "CMakeCache.txt").read_text(encoding="utf-8"), re.M)
    assert match
    directory = ROOT / "components" / "orpheus" / "builtin" / component
    executable = tmp_path / (component + ".exe")
    result = subprocess.run([
        match[1], "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DORPHEUS_BUILDING_COMPONENT", f"-DEMBED_INPUT={int(component == 'embed_in')}",
        "-I" + str(ROOT / "orpheus_abi" / "include"),
        "-I" + str(directory / "generated"), "-I" + str(directory / "user"),
        str(directory / "generated" / "component_shell.c"), str(directory / "user" / "algorithm.c"),
        str(ROOT / "tests" / "embed_boundary_smoke.c"), "-o", str(executable),
    ], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("channels,frames,double_bank", [(3, 32, "auto"), (32, 128, "off")])
def test_embed_host_ports_four_entries(tmp_path, channels, frames, double_bank):
    registry = Registry([ROOT / "components"])
    registry.scan()
    project = Project(flow_mode="task_sequence", block_size=frames, double_bank=double_bank,
        graph=Graph(nodes={
            "host_in": Node("host_in", "orpheus.builtin.embed_in", params={"channels": channels}),
            "host_out": Node("host_out", "orpheus.builtin.embed_out", params={"channels": channels}),
        }, connections=[Connection(PortRef("host_in", "out"), PortRef("host_out", "in"))]))
    plan = GraphCompiler(registry).compile(project)
    assert plan.tasks[0]["source"] == "host_in"
    assert plan.node_configs["host_in"]["output_drive_ports"] == {"out": 1}
    for nid in ("host_in", "host_out"):
        slot = next(item for item in plan.id_map if item["node"] == nid and item["key"] == "samples")
        assert slot["count"] == channels * frames
    result = execute_host_graph(tmp_path, registry, plan, {"host_in": channels}, {"host_out": channels})
    expected = [(((block * 17 + frame * 3 + channel * 13) % 257) - 128) * .0009765625
                for block in range(24) for frame in range(frames) for channel in range(channels)]
    assert result == struct.pack("<" + str(len(expected)) + "f", *expected)
    graph = (tmp_path / "generated" / "src" / "orpheus_graph.c").read_text(encoding="utf-8")
    platform = (tmp_path / "generated" / "src" / "platform_io.c").read_text(encoding="utf-8")
    assert "EmbedInState" not in graph and "EmbedOutState" not in graph and "g_embed_" not in graph
    assert 'orpheus_audio_submit("host_in", "out"' in platform
    assert 'orpheus_audio_read("host_out", "in"' in platform
    assert "orpheus_embed_in.h" not in platform and "orpheus_embed_out.h" not in platform


def test_platform_hook_errors_and_same_block_submission(tmp_path):
    registry = Registry([ROOT / "components"])
    registry.scan()
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "input": Node("input", "orpheus.builtin.embed_in"),
        "output": Node("output", "orpheus.builtin.embed_out"),
    }, connections=[Connection(PortRef("input", "out"), PortRef("output", "in"))]))
    plan = GraphCompiler(registry).compile(project)
    generator = CodeGenerator(registry, ROOT)
    directory = tmp_path / "generated"
    generator.generate(plan, directory)
    platform = r'''
#include <assert.h>
#include <string.h>
#include "orpheus_graph.h"
int embed_init_rc, embed_pre_rc, embed_post_rc, embed_posts;
float embed_sample;
const char* embed_expected_task;
int orpheus_platform_io_init(void) { return embed_init_rc; }
static void check_task(const char* task) {
    assert((!task && !embed_expected_task) ||
        (task && embed_expected_task && !strcmp(task, embed_expected_task)));
}
int orpheus_platform_io_pre_block(const char* task) {
    check_task(task);
    if (embed_pre_rc) return embed_pre_rc;
    float data[ORPHEUS_AUDIO_input_out_SAMPLES];
    for (unsigned i=0; i<ORPHEUS_AUDIO_input_out_SAMPLES; ++i) data[i]=embed_sample;
    return orpheus_audio_submit("input","out",data,ORPHEUS_AUDIO_input_out_SAMPLES);
}
int orpheus_platform_io_post_block(const char* task) {
    check_task(task);
    ++embed_posts;
    return embed_post_rc;
}
'''
    harness = r'''
#include <assert.h>
#include <string.h>
#include "orpheus_graph.h"
extern int embed_init_rc, embed_pre_rc, embed_post_rc, embed_posts;
extern float embed_sample;
extern const char* embed_expected_task;
int main(void) {
    embed_init_rc=-71;
    assert(orpheus_graph_init(48000,8)==-71);
    assert(!strcmp(orpheus_graph_last_error()->operation,"platform_io:init"));
    embed_init_rc=0;
    assert(orpheus_graph_init(48000,8)==0);
    for (unsigned mode=0; mode<2; ++mode) {
        int (*process)(uint32_t) = mode ? TASK_FUNCTION : orpheus_graph_process;
        embed_expected_task=mode ? TASK_ID : NULL;
        int posts=embed_posts;
        embed_pre_rc=-72;
        assert(process(8)==-72 && embed_posts==posts);
        assert(!strcmp(orpheus_graph_last_error()->operation,"platform_io:pre"));
        float output[ORPHEUS_AUDIO_output_in_SAMPLES];
        assert(orpheus_audio_read("output","in",output,ORPHEUS_AUDIO_output_in_SAMPLES)==ORPHEUS_ERR_PROCESSING);
        embed_pre_rc=0;
        embed_post_rc=-73;
        embed_sample=.25f+(float)mode;
        assert(process(8)==-73 && embed_posts==posts+1);
        assert(!strcmp(orpheus_graph_last_error()->operation,"platform_io:post"));
        assert(orpheus_audio_read("output","in",output,ORPHEUS_AUDIO_output_in_SAMPLES)==0);
        for (unsigned i=0; i<ORPHEUS_AUDIO_output_in_SAMPLES; ++i) assert(output[i]==embed_sample);
        embed_post_rc=0;
        embed_sample=-.5f-(float)mode;
        assert(process(8)==0 && embed_posts==posts+2);
        assert(orpheus_audio_read("output","in",output,ORPHEUS_AUDIO_output_in_SAMPLES)==0);
        for (unsigned i=0; i<ORPHEUS_AUDIO_output_in_SAMPLES; ++i) assert(output[i]==embed_sample);
    }
    orpheus_graph_teardown();
    return 0;
}
'''
    task = plan.tasks[0]["id"]
    harness = harness.replace("TASK_FUNCTION", "orpheus_graph_process_task_" + generator._sanitized_node_id(task))
    harness = harness.replace("TASK_ID", generator._c_string_literal(task))
    (directory / "src" / "platform_io.c").write_text(platform, encoding="utf-8", newline="\n")
    (directory / "src" / "embed_hook_test.c").write_text(harness, encoding="utf-8", newline="\n")
    with (directory / "CMakeLists.txt").open("a", encoding="utf-8", newline="\n") as stream:
        stream.write("\nadd_executable(embed_hook_test src/embed_hook_test.c)\n"
                     "target_link_libraries(embed_hook_test PRIVATE orpheus_graph)\n")
    _build(directory, tmp_path)
    result = subprocess.run([str(directory / "build" / "embed_hook_test.exe")],
                            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
