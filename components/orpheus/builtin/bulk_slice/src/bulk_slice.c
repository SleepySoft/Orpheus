#include "orpheus_bulk_slice.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="input_channels",.name="输入通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="input_elements",.name="输入每通道元素数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=129},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="channels",.name="输出通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="输出每通道元素数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=129},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="offset",.name="首槽索引",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
     .min_i32=0,.max_i32=2147483647,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED},
    {.id="stride",.name="槽步长",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=2147483647,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="input_channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_slice",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=params,.param_count=6,.state_size=sizeof(BulkSliceState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(BulkSliceState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkSliceState));
    ((BulkSliceState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((BulkSliceState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={1,129,1,129,0,1};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;while (p<6 && strcmp(key,params[p].id)) ++p;
        if (p==6 || value->type!=ORPHEUS_VALUE_INT || value->value.i32<params[p].min_i32 || value->value.i32>params[p].max_i32)
            return ORPHEUS_ERR_INVALID_ARG;
        values[p]=(uint32_t)value->value.i32;
    }
    uint64_t count=(uint64_t)values[2]*values[3];
    if ((uint64_t)values[4]+(count-1)*values[5]>=(uint64_t)values[0]*values[1])
        return ORPHEUS_ERR_INVALID_ARG;
    memcpy(((BulkSliceState*)state)->values,values,sizeof(values));return ORPHEUS_OK;
}
static int reset(void* state) {return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned p=0;p<6;++p) if (!strcmp(key,params[p].id)) {
        value->type=ORPHEUS_VALUE_INT;value->value.i32=(int32_t)((BulkSliceState*)state)->values[p];return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_INVALID_ARG;
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
    const uint32_t* v=((const BulkSliceState*)state)->values;
    if (!v[0]) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    const OrpheusBuffer* in=context->inputs[0];OrpheusBuffer* out=context->outputs[0];
    if (!valid(in,v[0],v[1],true)) return ORPHEUS_ERR_INVALID_ARG;
    if (!out) return orpheus_process_next(context);
    size_t input_bytes=(size_t)v[0]*v[1]*sizeof(float),count=(size_t)v[2]*v[3];
    if (!valid(out,v[2],v[3],false) || overlap(in->data,input_bytes,out->data,count*sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    const unsigned char* src=(const unsigned char*)in->data;unsigned char* dst=(unsigned char*)out->data;
    for (size_t i=0;i<count;++i)
        memcpy(dst+i*sizeof(float),src+((size_t)v[4]+i*v[5])*sizeof(float),sizeof(float));
    out->frame_count=v[3];return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
