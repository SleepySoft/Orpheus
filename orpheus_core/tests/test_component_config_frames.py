"""资源形状取节点prepare配置帧数，不取处理调用的Task量子。"""
import copy
import json

import jsonschema
import pytest

from orpheus_core.component_codegen import check_generated, generate_component
from orpheus_core.component_definition import ComponentDefinitionError, normalize_component_definition
from test_component_codegen import (
    ROOT,
    _compile_component_harness,
    _compile_cpp_component_algorithm_harness,
    package,
    raw,
    save,
)


@pytest.mark.parametrize("cpp", [False, True])
def test_resource_config_frames_are_bounded_and_transactional(package, raw, cpp):
    raw["algorithm"] = {"lifecycle": ["normalize", "validate", "prepare"]}
    raw["interfaces"]["audio_storage"] = {
        "type": "string", "default": "", "capacity": 64,
        "shape": ["config:frames", "param:channels"],
        "bulk": {"sample_format": "f32", "capacity": 16, "storage": "algorithm.samples", "slot_key": "samples"},
    }
    raw["interfaces"]["tamper"] = {"type": "bool", "default": False}
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    jsonschema.validate(raw, schema)
    generate_component(save(package, raw))
    check_generated(package / "component.yaml")
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
int test_pilot_normalize(test_pilot_Parameters* p,const char* key) {
    (void)key;if(p->tamper) ++p->_orpheus_config_frames;
    return ORPHEUS_OK;
}
int test_pilot_validate(const test_pilot_Parameters* p) {
    return p->_orpheus_config_frames>0 && p->_orpheus_config_frames<=8 ?
        ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}
int test_pilot_prepare(test_pilot_State* s,const OrpheusConfig* config) {
    return s->_orpheus_config_frames==config->block_size ? ORPHEUS_OK : ORPHEUS_ERR_PROCESSING;
}
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;(void)io;(void)ctx;return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
static unsigned seen,count;
static OrpheusSlotId observe(void* user,const OrpheusSlotInfo* slot) {
    (void)user;
    if(slot->kind==ORPHEUS_SLOT_BULK) {
        assert(!strcmp(slot->key,"samples"));
        assert(slot->count==count);++seen;
    }
    return 0;
}
int main(void) {
    test_pilot_State state;memset(&state,0xA5,sizeof(state));
    unsigned char before[sizeof(state)];memcpy(before,&state,sizeof(state));
    const OrpheusComponentInterface* api=orpheus_get_interface();
    OrpheusConfig config={.state_block=&state,.block_size=0};void* instance=&state;
    assert(api->create(&instance,&config)==ORPHEUS_ERR_INVALID_ARG && !instance);
    assert(memcmp(before,&state,sizeof(state))==0);
    config.block_size=9;instance=&state;
    assert(api->create(&instance,&config)==ORPHEUS_ERR_INVALID_ARG && !instance);
    assert(memcmp(before,&state,sizeof(state))==0);
    config.block_size=4;
    assert(api->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    assert(api->prepare(instance,&config)==ORPHEUS_OK && state._orpheus_config_frames==4);
    OrpheusRegistry registry={.add=observe};count=8;
    assert(api->register_slots(instance,&registry)==ORPHEUS_OK && seen==1);
    memcpy(before,&state,sizeof(state));config.block_size=9;
    assert(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG);
    assert(memcmp(before,&state,sizeof(state))==0);
    OrpheusValue channels={.type=ORPHEUS_VALUE_INT,.value.i32=5};
    assert(api->set_parameter(instance,"channels",&channels)==ORPHEUS_ERR_INVALID_ARG);
    assert(memcmp(before,&state,sizeof(state))==0);
    OrpheusValue tamper={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    assert(api->set_parameter(instance,"tamper",&tamper)==ORPHEUS_ERR_INVALID_ARG);
    assert(memcmp(before,&state,sizeof(state))==0);
    config.block_size=8;
    assert(api->prepare(instance,&config)==ORPHEUS_OK && state._orpheus_config_frames==8);
    count=16;assert(api->register_slots(instance,&registry)==ORPHEUS_OK && seen==2);
    OrpheusValue gain={.type=ORPHEUS_VALUE_FLOAT,.value.f32=2};
    assert(api->set_parameter(instance,"gain",&gain)==ORPHEUS_OK && state._orpheus_config_frames==8);
    assert(api->reset(instance)==ORPHEUS_OK && state._orpheus_config_frames==8);
    assert(api->register_slots(instance,&registry)==ORPHEUS_OK && seen==3);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


@pytest.mark.parametrize("dimension", ["config:block_size", "config:channels", "task:frames", "config:frames*2"])
def test_only_config_frames_is_a_supported_resource_dimension(raw, dimension):
    candidate = copy.deepcopy(raw)
    candidate["interfaces"]["audio_storage"] = {
        "type": "string", "default": "", "shape": [dimension],
        "bulk": {"sample_format": "f32", "capacity": 16, "storage": "algorithm.samples"},
    }
    with pytest.raises(ComponentDefinitionError):
        normalize_component_definition(candidate)
    schema = json.loads((ROOT / "schemas" / "component_interface.schema.json").read_text(encoding="utf-8"))
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(candidate, schema)


def test_resource_shape_uses_node_frames_in_all_four_entries(tmp_path):
    import yaml

    from orpheus_core.project import Connection, Graph, Node, PortRef, Project
    from orpheus_core.registry import Registry
    from test_dynamic_interface_delivery import run_backend
    from test_interface_delivery import audio, compile_delivery_backends, create_component, scalar
    from test_table_interfaces import four_entry_harness

    source = create_component(tmp_path, "config_frames_source", {
        "out": audio("output", channels=2, block_size=4, drive_port_no=1),
    }, r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_config_frames_source_algorithm(orpheus_test_config_frames_source_State* s,
    orpheus_test_config_frames_source_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;
    for(unsigned i=0;i<8;i++) io->out.samples[i]=(float)i;
    int rc=orpheus_test_config_frames_source_emit_out(ctx,&io->out);
    return rc ? rc : orpheus_test_config_frames_source_emit_out(ctx,&io->out);
}
''', source=True)
    source_definition = yaml.safe_load(source.read_text(encoding="utf-8"))
    source_definition["scheduling"] = {"split": True}
    save(source.parent, source_definition)
    generate_component(source)
    capture = create_component(tmp_path, "config_frames_capture", {
        "in": audio("input", channels=2, block_size=4, process=True),
        "audio_storage": {
            "type": "string", "default": "", "shape": ["config:frames", 2],
            "bulk": {"sample_format": "f32", "capacity": 16, "storage": "algorithm.samples",
                     "slot_key": "samples"},
        },
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, r'''
#include "component_state.h"
int orpheus_test_config_frames_capture_algorithm(orpheus_test_config_frames_capture_State* s,
    orpheus_test_config_frames_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(s->_orpheus_config_frames!=4 || ctx->frame_count!=4 || ctx->valid_frames!=4 ||
        !io->in.buffer || io->in.buffer->frame_count!=4) return ORPHEUS_ERR_PROCESSING;
    for(unsigned i=0;i<8;i++) {
        if(io->in.samples[i]!=(float)i) return ORPHEUS_ERR_PROCESSING;
        s->algorithm.samples[i]=io->in.samples[i];
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
''')
    registry = Registry()
    for manifest in (source, capture):
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.config_frames_source"),
        "capture": Node("capture", "orpheus.test.config_frames_capture"),
    }, connections=[Connection(PortRef("source", "out"), PortRef("capture", "in"))]))
    case = compile_delivery_backends(tmp_path, registry, project, [source, capture], four_entry_harness())
    plan = case[2]
    assert plan.node_configs["capture"]["frames"] == 4
    assert plan.tasks[0]["block_size"] == 8
    slot = next(entry for entry in plan.id_map if entry["node"] == "capture" and entry["key"] == "samples")
    assert slot["count"] == 8 and slot["type"] == "float"
    for backend in ("dynamic", "generated_backend"):
        for mode in (0, 1):
            result, rows = run_backend(case, backend, mode=mode)
            assert result.returncode == 0, (backend, mode, result.stdout, result.stderr)
            assert rows == [[0, 2, 0, 4], [1, 4, 0, 12]]
