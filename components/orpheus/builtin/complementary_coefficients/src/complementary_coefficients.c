#include "orpheus_complementary_coefficients.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[]={
    {.id="channels",.name="输入通道数",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2},.min_i32=1,.max_i32=32,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="每通道元素数",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=129},.min_i32=1,.max_i32=65536,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="total",.name="互补总量",.type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0f},.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,
     .is_variable=true,.channels_param="channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,
     .is_variable=true}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.complementary_coefficients",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=params,.param_count=3,
    .state_size=sizeof(ComplementaryCoefficientsState),.alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state=config->state_block; memset(*state,0,sizeof(ComplementaryCoefficientsState));
    } else {
        *state=calloc(1,sizeof(ComplementaryCoefficientsState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((ComplementaryCoefficientsState*)*state)->owns_memory=true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((ComplementaryCoefficientsState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    int32_t channels=2,elements=129;
    float total=1.0f;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];
        const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(key,"total")) {
            if (value->type!=ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
            total=value->value.f32;
        } else {
            if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
            if (!strcmp(key,"channels")) channels=value->value.i32;
            else if (!strcmp(key,"elements")) elements=value->value.i32;
            else return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    if (channels<1 || channels>32 || elements<1 || elements>65536) return ORPHEUS_ERR_INVALID_ARG;
    ComplementaryCoefficientsState* s=(ComplementaryCoefficientsState*)state;
    s->channels=channels; s->elements=elements; s->total=total;
    return ORPHEUS_OK;
}
static int reset(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    (void)state; (void)key; (void)value; return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const ComplementaryCoefficientsState* s=(const ComplementaryCoefficientsState*)state;
    if (!strcmp(key,"total")) { value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->total; }
    else {
        value->type=ORPHEUS_VALUE_INT;
        if (!strcmp(key,"channels")) value->value.i32=s->channels;
        else if (!strcmp(key,"elements")) value->value.i32=s->elements;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        !context->input_count || !context->output_count) return ORPHEUS_ERR_INVALID_ARG;
    const ComplementaryCoefficientsState* s=(const ComplementaryCoefficientsState*)state;
    const OrpheusBuffer* in=context->inputs[0]; OrpheusBuffer* out=context->outputs[0];
    if (!in) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    if (!s->channels || !in->data || in->format!=ORPHEUS_FORMAT_F32 || !in->interleaved ||
        in->channels!=s->channels || in->frame_count!=s->elements || in->frame_capacity<s->elements)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!out) return orpheus_process_next(context);
    if (!out->data || out->format!=ORPHEUS_FORMAT_F32 || !out->interleaved ||
        out->channels!=s->channels*2 || out->frame_capacity<s->elements) return ORPHEUS_ERR_INVALID_ARG;
    size_t input_bytes=(size_t)s->elements*s->channels*sizeof(float),output_bytes=input_bytes*2;
    uintptr_t source=(uintptr_t)in->data,destination=(uintptr_t)out->data;
    if (destination>=source ? destination-source<input_bytes : source-destination<output_bytes)
        return ORPHEUS_ERR_INVALID_ARG;
    const float* input=(const float*)in->data; float* output=(float*)out->data;
    for (unsigned row=0;row<s->elements;++row) {
        memcpy(output+(size_t)row*s->channels*2,input+(size_t)row*s->channels,s->channels*sizeof(float));
        for (unsigned c=0;c<s->channels;++c)
            output[(size_t)row*s->channels*2+s->channels+c]=s->total-input[(size_t)row*s->channels+c];
    }
    out->frame_count=s->elements;
    return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
