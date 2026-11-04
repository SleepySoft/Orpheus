#include "orpheus_bulk_scale.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="每通道元素数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="gain",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=1,.block_size=1},
    {.id="integer_gain",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_I32,.channels=1,.block_size=1},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_scale",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=4,.params=params,.param_count=2,.state_size=sizeof(BulkScaleState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(BulkScaleState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkScaleState));
    ((BulkScaleState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((BulkScaleState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={1,1};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=!strcmp(key,"channels") ? 0 : !strcmp(key,"elements") ? 1 : 2;
        if (p==2 || value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
            return ORPHEUS_ERR_INVALID_ARG;
        values[p]=(uint32_t)value->value.i32;
    }
    BulkScaleState* s=(BulkScaleState*)state;s->channels=values[0];s->elements=values[1];return ORPHEUS_OK;
}
static int reset(void* state) {return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const BulkScaleState* s=(const BulkScaleState*)state;value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"channels")) value->value.i32=(int32_t)s->channels;
    else if (!strcmp(key,"elements")) value->value.i32=(int32_t)s->elements;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned elements,OrpheusSampleFormat format,bool input) {
    return b && b->data && b->format==format && b->interleaved &&
        b->channels==channels && b->frame_capacity>=elements && (!input || b->frame_count==elements);
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<nb : y-x<na;
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs || context->input_count<2 || context->output_count<1)
        return ORPHEUS_ERR_INVALID_ARG;
    const BulkScaleState* s=(const BulkScaleState*)state;
    if (!s->channels || !s->elements) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    const OrpheusBuffer* in=context->inputs[0];const OrpheusBuffer* gain=context->inputs[1];
    const OrpheusBuffer* integer=context->input_count>2 ? context->inputs[2] : NULL;
    if ((gain!=NULL)==(integer!=NULL) || !valid(in,s->channels,s->elements,ORPHEUS_FORMAT_F32,true))
        return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* scalar=gain ? gain : integer;
    if (!valid(scalar,1,1,gain ? ORPHEUS_FORMAT_F32 : ORPHEUS_FORMAT_I32,true)) return ORPHEUS_ERR_INVALID_ARG;
    OrpheusBuffer* out=context->outputs[0];if (!out) return orpheus_process_next(context);
    size_t count=(size_t)s->channels*s->elements,bytes=count*sizeof(float);
    if (!valid(out,s->channels,s->elements,ORPHEUS_FORMAT_F32,false) ||
        overlap(in->data,bytes,out->data,bytes) || overlap(scalar->data,sizeof(float),out->data,bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    float factor;
    if (gain) memcpy(&factor,gain->data,sizeof(float));
    else {int32_t value;memcpy(&value,integer->data,sizeof(value));factor=(float)value;}
    const unsigned char* src=(const unsigned char*)in->data;unsigned char* dst=(unsigned char*)out->data;
    for (size_t i=0;i<count;++i) {
        float value;memcpy(&value,src+i*sizeof(float),sizeof(float));value=value*factor;
        memcpy(dst+i*sizeof(float),&value,sizeof(float));
    }
    out->frame_count=s->elements;return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
