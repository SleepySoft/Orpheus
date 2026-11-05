#include "orpheus_one_pole_envelope.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[] = {
    {.id="channels",.name="包络通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=20},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
#define VECTOR(ID,DIR) {.id=ID,.direction=DIR,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels",.block_size=1}
static const OrpheusPort ports[] = {
    VECTOR("start",ORPHEUS_PORT_INPUT),VECTOR("target",ORPHEUS_PORT_INPUT),
    {.id="alpha",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=1,.block_size=1},
    {.id="freeze",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_I32,.channels=1,.block_size=1},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    VECTOR("gains",ORPHEUS_PORT_OUTPUT)
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.one_pole_envelope",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=1,.ports=ports,.port_count=6,.state_size=sizeof(OnePoleEnvelopeState),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external?config->state_block:calloc(1,sizeof(OnePoleEnvelopeState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(OnePoleEnvelopeState));((OnePoleEnvelopeState*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* state) {if (state && ((OnePoleEnvelopeState*)state)->owns_memory) free(state);return 0;}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values))) return ORPHEUS_ERR_INVALID_ARG;
    int32_t n=20;
    for (unsigned i=0;i<config->param_count;++i) {
        const OrpheusValue* v=config->param_values+i;
        if (!config->param_ids[i] || strcmp(config->param_ids[i],"channels") || v->type!=ORPHEUS_VALUE_INT || v->value.i32<1 || v->value.i32>128)
            return ORPHEUS_ERR_INVALID_ARG;
        n=v->value.i32;
    }
    ((OnePoleEnvelopeState*)state)->channels=n;return 0;
}
static int reset(void* state) {return state?0:ORPHEUS_ERR_INVALID_ARG;}
static bool valid(const OrpheusBuffer* b,unsigned n,unsigned frames,OrpheusSampleFormat format,bool ready) {
    return b && b->data && b->channels==n && b->format==format && b->interleaved &&
        b->frame_capacity>=frames && (!ready || b->frame_count==frames);
}
static int process(void* state,const OrpheusProcessContext* c) {
    if (!state || !c || !c->inputs || !c->outputs || c->input_count<4 || c->output_count<2) return ORPHEUS_ERR_INVALID_ARG;
    if (!c->frame_count) return orpheus_process_next(c);
    unsigned n=(unsigned)((OnePoleEnvelopeState*)state)->channels,frames=c->frame_count;
    for (unsigned i=0;i<4;++i)
        if (!valid(c->inputs[i],i<2?n:1,1,i==3?ORPHEUS_FORMAT_I32:ORPHEUS_FORMAT_F32,true)) return ORPHEUS_ERR_INVALID_ARG;
    if ((c->outputs[0] && !valid(c->outputs[0],n,frames,ORPHEUS_FORMAT_F32,false)) ||
        (c->outputs[1] && !valid(c->outputs[1],n,1,ORPHEUS_FORMAT_F32,false))) return ORPHEUS_ERR_INVALID_ARG;
    float alpha;int32_t freeze;memcpy(&alpha,c->inputs[2]->data,4);memcpy(&freeze,c->inputs[3]->data,4);
    float gains[128];memcpy(gains,c->inputs[0]->data,n*4);const float* targets=c->inputs[1]->data;
    float* out=c->outputs[0]?(float*)c->outputs[0]->data:NULL;
    for (unsigned f=0;f<frames;++f) for (unsigned i=0;i<n;++i) {
        if (!freeze) {
            volatile float feed=(1.0f-alpha)*targets[i],feedback=alpha*gains[i];
            gains[i]=feed+feedback;
        }
        if (out) out[f*n+i]=gains[i];
    }
    if (c->outputs[0]) c->outputs[0]->frame_count=frames;
    if (c->outputs[1]) {memcpy(c->outputs[1]->data,gains,n*4);c->outputs[1]->frame_count=1;}
    return orpheus_process_next(c);
}
static const OrpheusComponentInterface api = {
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &api;}
