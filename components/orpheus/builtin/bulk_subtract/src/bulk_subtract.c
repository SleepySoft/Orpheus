#include "orpheus_bulk_subtract.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=64,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="每通道元素数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=129},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
#define PORT(key,dir) {.id=key,.direction=dir,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
static const OrpheusPort ports[]={PORT("a",ORPHEUS_PORT_INPUT),PORT("b",ORPHEUS_PORT_INPUT),PORT("out",ORPHEUS_PORT_OUTPUT)};
#undef PORT
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_subtract",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=3,.params=params,.param_count=2,.state_size=sizeof(BulkSubtractState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(BulkSubtractState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkSubtractState));
    ((BulkSubtractState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((BulkSubtractState*)state)->owns_memory) free(state);return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={1,129};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=!strcmp(key,"channels") ? 0 : !strcmp(key,"elements") ? 1 : 2;
        if (p==2 || value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
            return ORPHEUS_ERR_INVALID_ARG;
        values[p]=(uint32_t)value->value.i32;
    }
    BulkSubtractState* s=(BulkSubtractState*)state;s->channels=values[0];s->elements=values[1];return ORPHEUS_OK;
}
static int reset(void* state) {return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const BulkSubtractState* s=(const BulkSubtractState*)state;value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"channels")) value->value.i32=s->channels;
    else if (!strcmp(key,"elements")) value->value.i32=s->elements;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned frames,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==channels && b->frame_capacity>=frames && (!input || b->frame_count==frames);
}
static bool overlap(const void* a,const void* b,size_t bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<bytes : y-x<bytes;
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<2 || context->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    const BulkSubtractState* s=(const BulkSubtractState*)state;
    if (!s->channels || s->channels>64 || !s->elements || s->elements>65536) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    size_t count=(size_t)s->channels*s->elements,bytes=count*sizeof(float);
    OrpheusBuffer* out=context->outputs[0];
    if (out && !valid(out,s->channels,s->elements,false)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned p=0;p<2;++p)
        if (!valid(context->inputs[p],s->channels,s->elements,true) ||
            (out && overlap(context->inputs[p]->data,out->data,bytes))) return ORPHEUS_ERR_INVALID_ARG;
    if (!out) return orpheus_process_next(context);
    const float* a=(const float*)context->inputs[0]->data;const float* b=(const float*)context->inputs[1]->data;
    float* output=(float*)out->data;
    for (size_t i=0;i<count;++i) output[i]=a[i]-b[i];
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
