#include "orpheus_bulk_sum.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="每通道元素数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=65},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
#define PORT(key,dir) {.id=key,.direction=dir,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
static const OrpheusPort ports[]={PORT("in",ORPHEUS_PORT_INPUT),PORT("out",ORPHEUS_PORT_OUTPUT)};
#undef PORT
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_sum",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=params,.param_count=2,.state_size=sizeof(BulkSumState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(BulkSumState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkSumState));
    ((BulkSumState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((BulkSumState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={1,65};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=!strcmp(key,"channels") ? 0 : !strcmp(key,"elements") ? 1 : 2;
        if (p==2 || value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
            return ORPHEUS_ERR_INVALID_ARG;
        values[p]=(uint32_t)value->value.i32;
    }
    BulkSumState* s=(BulkSumState*)state;s->channels=values[0];s->elements=values[1];return ORPHEUS_OK;
}
static int reset(void* state) {return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const BulkSumState* s=(const BulkSumState*)state;value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"channels")) value->value.i32=(int32_t)s->channels;
    else if (!strcmp(key,"elements")) value->value.i32=(int32_t)s->elements;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned elements,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==channels && b->frame_capacity>=elements && (!input || b->frame_count==elements);
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<nb : y-x<na;
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs || context->input_count<1 || context->output_count<1)
        return ORPHEUS_ERR_INVALID_ARG;
    const BulkSumState* s=(const BulkSumState*)state;
    if (!s->channels || !s->elements) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    const OrpheusBuffer* in=context->inputs[0];OrpheusBuffer* out=context->outputs[0];
    if (!valid(in,s->channels,s->elements,true)) return ORPHEUS_ERR_INVALID_ARG;
    if (!out) return orpheus_process_next(context);
    size_t output_bytes=(size_t)s->channels*sizeof(float);
    if (!valid(out,s->channels,1,false) || overlap(in->data,output_bytes*s->elements,out->data,output_bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    const unsigned char* src=(const unsigned char*)in->data;unsigned char* dst=(unsigned char*)out->data;
    if (s->elements==1) memcpy(dst,src,output_bytes);
    else for (unsigned c=0;c<s->channels;++c) {
        float value;memcpy(&value,src+(size_t)c*sizeof(float),sizeof(float));
        volatile float sum=value;
        for (unsigned i=1;i<s->elements;++i) {
            memcpy(&value,src+((size_t)i*s->channels+c)*sizeof(float),sizeof(float));
            sum=sum+value;
        }
        value=sum;memcpy(dst+(size_t)c*sizeof(float),&value,sizeof(float));
    }
    out->frame_count=1;return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
