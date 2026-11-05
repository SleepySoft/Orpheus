#include "orpheus_sample_product.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[] = {
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=20},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
static const OrpheusPort ports[] = {
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="gain",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.sample_product",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=1,.ports=ports,.port_count=3,.state_size=sizeof(SampleProductState),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external?config->state_block:calloc(1,sizeof(SampleProductState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(SampleProductState));((SampleProductState*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* state) {if (state && ((SampleProductState*)state)->owns_memory) free(state);return 0;}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values))) return ORPHEUS_ERR_INVALID_ARG;
    int32_t n=20;
    for (unsigned i=0;i<config->param_count;++i) {
        const OrpheusValue* v=config->param_values+i;
        if (!config->param_ids[i] || strcmp(config->param_ids[i],"channels") || v->type!=ORPHEUS_VALUE_INT || v->value.i32<1 || v->value.i32>128)
            return ORPHEUS_ERR_INVALID_ARG;
        n=v->value.i32;
    }
    ((SampleProductState*)state)->channels=n;return 0;
}
static int reset(void* state) {return state?0:ORPHEUS_ERR_INVALID_ARG;}
static int process(void* state,const OrpheusProcessContext* c) {
    if (!state || !c || !c->inputs || !c->outputs || c->input_count<2 || c->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    if (!c->frame_count) return orpheus_process_next(c);
    unsigned n=(unsigned)((SampleProductState*)state)->channels,frames=c->frame_count;
    for (unsigned i=0;i<3;++i) {
        const OrpheusBuffer* b=i<2?c->inputs[i]:c->outputs[0];
        if (i==2 && !b) continue;
        if (!b || !b->data || b->channels!=n || b->format!=ORPHEUS_FORMAT_F32 || !b->interleaved ||
            b->frame_capacity<frames || (i<2 && b->frame_count!=frames)) return ORPHEUS_ERR_INVALID_ARG;
    }
    if (c->outputs[0]) {
        const float* a=c->inputs[0]->data;const float* b=c->inputs[1]->data;float* out=c->outputs[0]->data;
        for (size_t i=0;i<(size_t)n*frames;++i) out[i]=a[i]*b[i];
        c->outputs[0]->frame_count=frames;
    }
    return orpheus_process_next(c);
}
static const OrpheusComponentInterface api = {
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &api;}
