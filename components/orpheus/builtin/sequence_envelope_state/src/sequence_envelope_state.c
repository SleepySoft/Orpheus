#include "orpheus_sequence_envelope_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="channels",.name="包络通道数",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=20},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="silent",.name="新偶序列增益下界",.type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=5.0118723E-7f},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
};
#define VECTOR(ID,DIR) {.id=ID,.direction=DIR,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels",.block_size=1}
#define SCALAR(ID,DIR,FMT) {.id=ID,.direction=DIR,.type=ORPHEUS_PORT_BULK,.sample_format=FMT,.channels=1,.block_size=1}
static const OrpheusPort ports[] = {
    VECTOR("targets",ORPHEUS_PORT_INPUT),
    SCALAR("sequence",ORPHEUS_PORT_INPUT,ORPHEUS_FORMAT_I32),
    SCALAR("coefficient",ORPHEUS_PORT_INPUT,ORPHEUS_FORMAT_F32),
    {.id="feedback",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels",.block_size=1},
    VECTOR("start",ORPHEUS_PORT_OUTPUT), VECTOR("accepted",ORPHEUS_PORT_OUTPUT),
    SCALAR("alpha",ORPHEUS_PORT_OUTPUT,ORPHEUS_FORMAT_F32),
    SCALAR("freeze",ORPHEUS_PORT_OUTPUT,ORPHEUS_FORMAT_I32)
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.sequence_envelope_state",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=8,.params=params,.param_count=2,.state_size=sizeof(SequenceEnvelopeState),
    .alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external?config->state_block:calloc(1,sizeof(SequenceEnvelopeState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(SequenceEnvelopeState));((SequenceEnvelopeState*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* state) {if (state && ((SequenceEnvelopeState*)state)->owns_memory) free(state);return 0;}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    SequenceEnvelopeState* s=state;s->initialized=false;s->sequence=0;
    memset(s->current,0,sizeof(s->current));memset(s->targets,0,sizeof(s->targets));s->coefficient=0;return 0;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values))) return ORPHEUS_ERR_INVALID_ARG;
    SequenceEnvelopeState* s=state;int32_t channels=20;float silent=5.0118723E-7f;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* k=config->param_ids[i];const OrpheusValue* v=config->param_values+i;
        if (!k) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(k,"channels")) {
            if (v->type!=ORPHEUS_VALUE_INT || v->value.i32<1 || v->value.i32>128) return ORPHEUS_ERR_INVALID_ARG;
            channels=v->value.i32;
        } else if (!strcmp(k,"silent")) {
            if (v->type!=ORPHEUS_VALUE_FLOAT || !isfinite(v->value.f32) || v->value.f32<0) return ORPHEUS_ERR_INVALID_ARG;
            silent=v->value.f32;
        } else return ORPHEUS_ERR_INVALID_ARG;
    }
    s->channels=channels;s->silent=silent;return reset(s);
}
static bool valid(const OrpheusBuffer* b,unsigned channels,OrpheusSampleFormat format,bool ready) {
    return b && b->data && b->channels==channels && b->format==format && b->interleaved &&
        b->frame_capacity>=1 && (!ready || b->frame_count==1);
}
static int process(void* state,const OrpheusProcessContext* c) {
    if (!state || !c || !c->inputs || !c->outputs || c->input_count<4 || c->output_count<4) return ORPHEUS_ERR_INVALID_ARG;
    if (!c->frame_count) return orpheus_process_next(c);
    SequenceEnvelopeState* s=state;unsigned n=(unsigned)s->channels;
    if (!n || !valid(c->inputs[0],n,ORPHEUS_FORMAT_F32,true) ||
        !valid(c->inputs[1],1,ORPHEUS_FORMAT_I32,true) || !valid(c->inputs[2],1,ORPHEUS_FORMAT_F32,true) ||
        !valid(c->inputs[3],n,ORPHEUS_FORMAT_F32,false)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=0;i<4;++i)
        if (c->outputs[i] && !valid(c->outputs[i],i<2?n:1,i==3?ORPHEUS_FORMAT_I32:ORPHEUS_FORMAT_F32,false)) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t sequence;memcpy(&sequence,c->inputs[1]->data,4);int32_t freeze=(sequence&1U)!=0;
    if (!s->initialized) {
        memcpy(s->current,c->inputs[0]->data,n*4);memcpy(s->targets,c->inputs[0]->data,n*4);
        memcpy(&s->coefficient,c->inputs[2]->data,4);s->initialized=true;
    }
    if (sequence!=s->sequence && !freeze) {
        for (unsigned i=0;i<n;++i) s->current[i]=fmaxf(s->current[i],s->silent);
        memcpy(s->targets,c->inputs[0]->data,n*4);
    }
    s->sequence=sequence;
    const void* values[]={s->current,s->targets,&s->coefficient,&freeze};
    for (unsigned i=0;i<4;++i) if (c->outputs[i]) {
        memcpy(c->outputs[i]->data,values[i],i<2?n*4:4);c->outputs[i]->frame_count=1;
    }
    int result=orpheus_process_next(c);if (result) return result;
    if (!valid(c->inputs[3],n,ORPHEUS_FORMAT_F32,true)) return ORPHEUS_ERR_INVALID_ARG;
    memcpy(s->current,c->inputs[3]->data,n*4);return 0;
}
static const OrpheusComponentInterface api = {
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &api;}
