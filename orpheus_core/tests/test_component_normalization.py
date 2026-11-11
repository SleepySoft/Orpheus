"""候选规范化必须先于验证和状态提交，且保留声明约束。"""
import pytest

from orpheus_core.component_codegen import generate_component
from test_component_codegen import (
    _compile_component_harness,
    _compile_cpp_component_algorithm_harness,
    package,
    raw,
    save,
)


@pytest.mark.parametrize("cpp", [False, True])
def test_normalized_candidates_are_checked_and_committed_atomically(package, raw, cpp):
    raw["algorithm"] = {"lifecycle": ["normalize", "validate", "set_parameter"]}
    raw["interfaces"] = {
        "channels": {"type": "int", "default": 2, "range": [1, 8]},
        "selected": {"type": "int", "default": 1},
        "index": {"type": "int", "default": -1},
        "gain": {"type": "float", "default": 1, "range": [0, 4]},
        "label": {"type": "string", "default": "ready", "capacity": 16},
        "weights": {"type": "float", "count": 2, "default": [.25, .75], "range": [0, 1]},
    }
    generate_component(save(package, raw))
    (package / "user" / "algorithm.c").write_text(r'''
#include "component_state.h"
#include <math.h>
#include <string.h>
int test_pilot_normalize(test_pilot_Parameters* p,const char* key) {
    if(p->index==-99) { p->selected=1;return ORPHEUS_ERR_PROCESSING; }
    if(p->index==-98) p->gain=NAN;
    if(p->index==-97) p->channels=9;
    if(p->index==-96) memset(p->label,'x',sizeof(p->label));
    if(p->index==-95) p->weights[1]=2;
    if(p->index==-94) p->weights[1]=NAN;
    int selected=(!key && p->index>=0) || (key && !strcmp(key,"index")) ?
        (p->index<0 ? 1 : p->index>=p->channels ? p->channels : p->index+1) : p->selected;
    if(selected<1) selected=1;
    if(selected>p->channels) selected=p->channels;
    p->selected=selected;p->index=selected-1;
    return ORPHEUS_OK;
}
int test_pilot_validate(const test_pilot_Parameters* p) {
    return p->selected>=1 && p->selected<=p->channels && p->index==p->selected-1 ?
        ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}
int test_pilot_set_parameter(test_pilot_State* s,const char* key,const OrpheusValue* value) {
    (void)s;
    if(!strcmp(key,"selected") && value->value.i32!=2) return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int test_pilot_algorithm(test_pilot_State* s,test_pilot_IO* io,const OrpheusProcessContext* ctx) {
    (void)s;(void)io;(void)ctx;return ORPHEUS_OK;
}
''', encoding="utf-8", newline="\n")
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    test_pilot_State state;void* instance=NULL;
    const char* keys[]={"selected"};
    OrpheusValue requested={.type=ORPHEUS_VALUE_INT,.value.i32=7};
    OrpheusConfig config={.state_block=&state,.param_count=1,.param_ids=keys,.param_values=&requested};
    assert(api->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    assert(state.selected==2 && state.index==1);
    assert(api->prepare(instance,&config)==ORPHEUS_OK);
    requested.value.i32=0;
    assert(api->set_parameter(instance,"index",&requested)==ORPHEUS_OK);
    assert(state.selected==1 && state.index==0);
    requested.value.i32=2147483647;
    assert(api->set_parameter(instance,"selected",&requested)==ORPHEUS_OK);
    assert(state.selected==2 && state.index==1);
    OrpheusValue actual;
    assert(api->get_parameter(instance,"selected",&actual)==ORPHEUS_OK && actual.value.i32==2);
    unsigned char before[sizeof(state)];memcpy(before,&state,sizeof(state));
    keys[0]="index";
    for(int bad=-99;bad<=-94;bad++) {
        requested.value.i32=bad;
        int error=bad==-99 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_ERR_INVALID_ARG;
        assert(api->set_parameter(instance,"index",&requested)==error);
        assert(memcmp(before,&state,sizeof(state))==0);
        assert(api->prepare(instance,&config)==error);
        assert(memcmp(before,&state,sizeof(state))==0);
        void* rejected=&state;
        assert(api->create(&rejected,&config)==error && !rejected);
        assert(memcmp(before,&state,sizeof(state))==0);
    }
    requested.type=ORPHEUS_VALUE_FLOAT;requested.value.f32=1;
    assert(api->set_parameter(instance,"index",&requested)==ORPHEUS_ERR_INVALID_ARG);
    assert(memcmp(before,&state,sizeof(state))==0);
    assert(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}
''', encoding="utf-8", newline="\n")
    (_compile_cpp_component_algorithm_harness if cpp else _compile_component_harness)(package)


def test_normalize_only_default_template_exposes_candidate_type(package, raw):
    raw["algorithm"] = {"lifecycle": ["normalize"]}
    generate_component(save(package, raw))
    (package / "harness.c").write_text(r'''
#include "component_state.h"
#include <assert.h>
int main(void) {
    test_pilot_State state;void* instance=NULL;
    OrpheusConfig config={.state_block=&state};
    assert(orpheus_get_interface()->create(&instance,&config)==ORPHEUS_OK);
    assert(orpheus_get_interface()->prepare(instance,&config)==ORPHEUS_OK);
    assert(orpheus_get_interface()->destroy(instance)==ORPHEUS_OK);
    return 0;
}
''', encoding="utf-8", newline="\n")
    _compile_component_harness(package)
