"""采样率比例声明、公开Schema与独立生成脚本的定向验收。"""
import json
import subprocess

import jsonschema
import pytest

from orpheus_core.component_definition import ComponentDefinitionError, normalize_component_definition
from test_component_codegen import (
    PYTHON, ROOT, _compile_component_harness, _compile_cpp_component_algorithm_harness,
    package, raw, save,
)


@pytest.mark.parametrize("expression", [
    "in:sample_rate/param:channels",
    "in:sample_rate*param:channels/3",
    "task:sample_rate/3*param:channels",
    "in:sample_rate/4294967294/4294967294",
])
def test_rate_scaling_declarations_and_public_schema(raw, expression):
    raw["interfaces"]["out"]["sample_rate"] = expression
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    definition = normalize_component_definition(raw)
    assert definition["ports"][1]["sample_rate"] == expression


@pytest.mark.parametrize("expression", [
    "in:sample_rate/0", "in:sample_rate/-1", "in:sample_rate/1.5",
    "in:sample_rate /2", "in:sample_rate//2", "in:sample_rate/param:channels\n",
    "in:sample_rate/param:channels+1", "in:sample_rate/in:channels",
])
def test_rate_scaling_rejects_invalid_syntax(raw, expression):
    raw["interfaces"]["out"]["sample_rate"] = expression
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition(raw)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(raw, schema)


@pytest.mark.parametrize("mutation", ["zero", "float", "not_signature", "cycle", "missing", "payload"])
def test_rate_scaling_rejects_invalid_dependencies(raw, mutation):
    raw["interfaces"]["out"]["sample_rate"] = "in:sample_rate/param:channels"
    if mutation == "zero":
        raw["interfaces"]["channels"]["range"][0] = 0
    elif mutation == "float":
        raw["interfaces"]["channels"]["type"] = "float"
    elif mutation == "not_signature":
        raw["interfaces"]["channels"]["affects_signature"] = False
    elif mutation == "cycle":
        raw["interfaces"]["in"]["sample_rate"] = "out:sample_rate"
    elif mutation == "missing":
        raw["interfaces"]["out"]["sample_rate"] = "in:sample_rate/param:absent"
    else:
        raw["interfaces"]["out"]["block_size"] = "task:sample_rate/param:channels"
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition(raw)


@pytest.mark.parametrize("cpp", [False, True])
def test_independent_cli_generates_rate_scaling_shell_for_c_and_cpp(package, raw, cpp):
    raw["interfaces"]["out"]["sample_rate"] = "in:sample_rate*param:channels/3"
    path = save(package, raw)
    result = subprocess.run(
        [str(PYTHON), "-X", "utf8", str(ROOT / "scripts" / "generate_component.py"), str(path)],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 0, (result.stdout, result.stderr)
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const OrpheusComponentDescriptor* desc=api->get_descriptor();
    assert(desc->ports[1].sample_rate==0);
    test_pilot_State state;void* handle=0;
    OrpheusConfig config={.sample_rate=48000,.block_size=8,.channels=2,.state_block=&state};
    assert(api->create(&handle,&config)==ORPHEUS_OK);
    assert(api->prepare(handle,&config)==ORPHEUS_OK);
    assert(api->destroy(handle)==ORPHEUS_OK);
    return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)
