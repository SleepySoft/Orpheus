"""有理载荷长度不做逐步整数截断，声明与覆盖值分别校验。"""
import json
import subprocess

import jsonschema
import pytest

from orpheus_core.compiler import CompileError, GraphCompiler
from orpheus_core.component_definition import ComponentDefinitionError, normalize_component_definition
from orpheus_core.project import Graph, Node, Project
from orpheus_core.registry import Registry
from test_component_codegen import (
    PYTHON, ROOT, _compile_component_harness, _compile_cpp_component_algorithm_harness,
    package, raw, save,
)


def ratio(raw):
    for key, default in (("frames", 9), ("up", 2), ("down", 3)):
        raw["interfaces"][key] = {
            "type": "int", "default": default, "range": [1, 64],
            "update_policy": "restart_required", "affects_signature": True,
        }
    raw["interfaces"]["in"]["block_size"] = "param:frames"
    raw["interfaces"]["out"]["block_size"] = "param:frames*param:up/param:down"
    return raw


@pytest.mark.parametrize("expression", ["param:frames*param:up/param:down", "param:frames/3*2", "9/3", "2/3*9"])
def test_payload_ratio_schema_and_default_exactness(raw, expression):
    ratio(raw)["interfaces"]["out"]["block_size"] = expression
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    assert normalize_component_definition(raw)["ports"][1]["block_size"] == expression


@pytest.mark.parametrize("mutation", ["fraction", "zero", "type", "signature", "overflow", "cycle", "channels"])
def test_payload_ratio_invalid_semantics(raw, mutation):
    ratio(raw)
    if mutation == "fraction":
        raw["interfaces"]["frames"]["default"] = 10
    elif mutation == "zero":
        raw["interfaces"]["down"]["range"][0] = 0
    elif mutation == "type":
        raw["interfaces"]["down"]["type"] = "float"
    elif mutation == "signature":
        raw["interfaces"]["down"]["affects_signature"] = False
    elif mutation == "overflow":
        raw["interfaces"]["frames"]["range"][1] = 2147483647
    elif mutation == "cycle":
        raw["interfaces"]["in"]["block_size"] = "out:block_size"
        raw["interfaces"]["out"]["block_size"] = "in:block_size"
    else:
        raw["interfaces"]["out"]["channels"] = "param:frames/param:down"
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition(raw)


@pytest.mark.parametrize("frames,expected", [(12, 8), (10, None)])
def test_graph_overrides_do_not_truncate_payload_ratios(package, raw, frames, expected):
    ratio(raw)
    del raw["interfaces"]["in"]
    raw["clock_source"] = True
    registry = Registry()
    registry.add_manifest(save(package, raw))
    project = Project(flow_mode="task_sequence", graph=Graph(nodes={
        "source": Node("source", "test.pilot", params={"frames": frames}),
    }))
    if expected is None:
        with pytest.raises(CompileError, match="正整数.*不得截断"):
            GraphCompiler(registry).compile(project)
    else:
        plan = GraphCompiler(registry).compile(project)
        assert plan.node_configs["source"]["output_port_block_sizes"]["out"] == expected


@pytest.mark.parametrize("cpp", [False, True])
def test_independent_cli_and_real_ratio_payload_views(package, raw, cpp):
    ratio(raw)
    path = save(package, raw)
    result = subprocess.run(
        [str(PYTHON), "-X", "utf8", str(ROOT / "scripts" / "generate_component.py"), str(path)],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 0, (result.stdout, result.stderr)
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include "component_api.h"
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    if(ctx->frame_count!=11 || io->in.buffer->frame_count!=(uint32_t)s->frames ||
       io->out.buffer->frame_count!=(uint32_t)(s->frames*s->up/s->down)) return ORPHEUS_ERR_PROCESSING;
    return test_pilot_emit_out(ctx,&io->out);
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* handle=0;
    OrpheusConfig config={.sample_rate=48000,.block_size=11,.channels=2,.state_block=&state};
    assert(api->create(&handle,&config)==ORPHEUS_OK && api->prepare(handle,&config)==ORPHEUS_OK);
    float data[18]={0},output[12]={0};
    OrpheusBuffer in={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_count=9,.frame_capacity=9,.interleaved=true};
    OrpheusBuffer out={.data=output,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_capacity=6,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=11};
    assert(api->process(handle,&ctx)==ORPHEUS_OK && out.frame_count==6);
    state.down=0;
    assert(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    state.down=3;state.up=1;
    assert(api->process(handle,&ctx)==ORPHEUS_OK && out.frame_count==3);
    state.frames=10;in.frame_count=in.frame_capacity=10;
    assert(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    assert(api->destroy(handle)==ORPHEUS_OK);
    return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)
