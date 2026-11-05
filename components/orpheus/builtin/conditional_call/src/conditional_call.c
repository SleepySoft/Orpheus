#include "orpheus_conditional_call.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[] = {
    {.id="enabled",.name="区域使能",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
};
static const OrpheusPort ports[] = {
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.channels=54,.block_size=32},
    {.id="returned",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=22,.block_size=32},
    {.id="returned_gains",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=80,.block_size=1},
    {.id="enable",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_I32,.channels=1,.block_size=1},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.channels=54,.block_size=32},
    {.id="result",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.channels=22,.block_size=32},
    {.id="gains",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=80,.block_size=1}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.conditional_call",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=7,.params=params,.param_count=1,.state_size=sizeof(ConditionalCallState),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external?config->state_block:calloc(1,sizeof(ConditionalCallState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(ConditionalCallState));((ConditionalCallState*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* state) {if (state && ((ConditionalCallState*)state)->owns_memory) free(state);return 0;}
static int set_parameter(void* state,const char* key,const OrpheusValue* v) {
    if (!state || !key || !v || strcmp(key,"enabled") || v->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
    ((ConditionalCallState*)state)->enabled=v->value.i32;return 0;
}
static int get_parameter(void* state,const char* key,OrpheusValue* v) {
    if (!state || !key || !v || strcmp(key,"enabled")) return ORPHEUS_ERR_INVALID_ARG;
    v->type=ORPHEUS_VALUE_INT;v->value.i32=((ConditionalCallState*)state)->enabled;return 0;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values))) return ORPHEUS_ERR_INVALID_ARG;
    ((ConditionalCallState*)state)->enabled=1;
    for (unsigned i=0;i<config->param_count;++i) {int r=set_parameter(state,config->param_ids[i],config->param_values+i);if (r) return r;}
    return 0;
}
static int reset(void* state) {return state?0:ORPHEUS_ERR_INVALID_ARG;}
static int register_slots(void* state,const OrpheusRegistry* reg) {
    if (!state || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    ConditionalCallState* s=state;
    ORPHEUS_REG_SLOT(reg,s,enabled,ORPHEUS_SLOT_SETTING,"enabled","区域使能",ORPHEUS_VALUE_INT,
        .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.flags=ORPHEUS_SLOT_READBACK);
    return 0;
}
static bool valid(const OrpheusBuffer* b,unsigned n,unsigned frames,bool ready) {
    return b && b->data && b->channels==n && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->frame_capacity>=frames && (!ready || b->frame_count==frames);
}
static int process(void* state,const OrpheusProcessContext* c) {
    if (!state || !c || !c->inputs || !c->outputs || c->input_count<3 || c->output_count<3 ||
        !c->continuation || c->continuation->count!=2) return ORPHEUS_ERR_INVALID_ARG;
    if (!c->frame_count) return 0;
    if (c->frame_count!=32 || !valid(c->inputs[0],54,32,true)) return ORPHEUS_ERR_INVALID_ARG;
    const unsigned counts[]={54,22,80},frames[]={32,32,1};
    for (unsigned i=0;i<3;++i)
        if (c->outputs[i] && !valid(c->outputs[i],counts[i],frames[i],false)) return ORPHEUS_ERR_INVALID_ARG;
    if (c->outputs[0]) {memcpy(c->outputs[0]->data,c->inputs[0]->data,54*32*4);c->outputs[0]->frame_count=32;}
    int32_t enabled=((ConditionalCallState*)state)->enabled;
    if (c->input_count>3 && c->inputs[3]) {
        const OrpheusBuffer* b=c->inputs[3];
        if (!b->data || b->format!=ORPHEUS_FORMAT_I32 || b->channels!=1 || !b->interleaved || b->frame_count!=1 || b->frame_capacity<1)
            return ORPHEUS_ERR_INVALID_ARG;
        memcpy(&enabled,b->data,4);
    }
    if (enabled) {
        int r=orpheus_process_branch(c,0);if (r) return r;
        if (!valid(c->inputs[1],22,32,true) || !valid(c->inputs[2],80,1,true)) return ORPHEUS_ERR_INVALID_ARG;
        for (unsigned i=1;i<3;++i) if (c->outputs[i])
            memcpy(c->outputs[i]->data,c->inputs[i]->data,counts[i]*frames[i]*4);
    } else {
        for (unsigned i=1;i<3;++i) if (c->outputs[i]) memset(c->outputs[i]->data,0,counts[i]*frames[i]*4);
    }
    for (unsigned i=1;i<3;++i) if (c->outputs[i]) c->outputs[i]->frame_count=frames[i];
    return orpheus_process_branch(c,1);
}
static const OrpheusComponentInterface api = {
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process,
    .set_parameter=set_parameter,.get_parameter=get_parameter,.register_slots=register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &api;}
