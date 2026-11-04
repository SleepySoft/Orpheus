#include "orpheus_bulk_deinterleave.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="每通道元素数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=129},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=1,.is_variable=true}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_deinterleave",.version="1.1.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=params,.param_count=2,.state_size=sizeof(BulkDeinterleaveState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(BulkDeinterleaveState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkDeinterleaveState));
    ((BulkDeinterleaveState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((BulkDeinterleaveState*)state)->owns_memory) free(state);return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={2,129};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=!strcmp(key,"channels") ? 0 : !strcmp(key,"elements") ? 1 : 2;
        if (p==2 || value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
            return ORPHEUS_ERR_INVALID_ARG;
        values[p]=(uint32_t)value->value.i32;
    }
    BulkDeinterleaveState* s=(BulkDeinterleaveState*)state;s->channels=values[0];s->elements=values[1];return ORPHEUS_OK;
}
static int reset(void* state) {return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const BulkDeinterleaveState* s=(const BulkDeinterleaveState*)state;value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"channels")) value->value.i32=s->channels;
    else if (!strcmp(key,"elements")) value->value.i32=s->elements;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned frames,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==channels && b->frame_capacity>=frames && (!input || b->frame_count==frames);
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<nb : y-x<na;
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs) return ORPHEUS_ERR_INVALID_ARG;
    const BulkDeinterleaveState* s=(const BulkDeinterleaveState*)state;
    if (!s->channels || !s->elements || context->input_count<1 || context->output_count<s->channels)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    const OrpheusBuffer* in=context->inputs[0];size_t mono=(size_t)s->elements*sizeof(float);
    if (!valid(in,s->channels,s->elements,true)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned c=0;c<s->channels;++c) {
        const OrpheusBuffer* out=context->outputs[c];
        if (!out) continue;
        if (!valid(out,1,s->elements,false) || overlap(in->data,mono*s->channels,out->data,mono)) return ORPHEUS_ERR_INVALID_ARG;
        for (unsigned p=0;p<c;++p)
            if (context->outputs[p] && overlap(context->outputs[p]->data,mono,out->data,mono)) return ORPHEUS_ERR_INVALID_ARG;
    }
    const unsigned char* src=(const unsigned char*)in->data;
    for (unsigned c=0;c<s->channels;++c) {
        OrpheusBuffer* out=context->outputs[c];if (!out) continue;
        for (unsigned i=0;i<s->elements;++i)
            memcpy((unsigned char*)out->data+(size_t)i*sizeof(float),src+((size_t)i*s->channels+c)*sizeof(float),sizeof(float));
        out->frame_count=s->elements;
    }
    return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
