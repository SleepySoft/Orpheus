#include "orpheus_complex_gain_split.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},.min_i32=1,.max_i32=64,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="每通道复数点数",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=129},.min_i32=1,.max_i32=65536,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="gain",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="impulse",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="reverb",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.complex_gain_split",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=4,.params=params,.param_count=2,.state_size=sizeof(ComplexGainSplitState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state=config->state_block; memset(*state,0,sizeof(ComplexGainSplitState));
    } else {
        *state=calloc(1,sizeof(ComplexGainSplitState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((ComplexGainSplitState*)*state)->owns_memory=true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((ComplexGainSplitState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    int32_t channels=1,elements=129;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];
        if (!key || config->param_values[i].type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(key,"channels")) channels=config->param_values[i].value.i32;
        else if (!strcmp(key,"elements")) elements=config->param_values[i].value.i32;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    if (channels<1 || channels>64 || elements<1 || elements>65536) return ORPHEUS_ERR_INVALID_ARG;
    ComplexGainSplitState* s=(ComplexGainSplitState*)state; s->channels=channels; s->elements=elements;
    return ORPHEUS_OK;
}
static int reset(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    (void)state; (void)key; (void)value; return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const ComplexGainSplitState* s=(const ComplexGainSplitState*)state;
    value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"channels")) value->value.i32=s->channels;
    else if (!strcmp(key,"elements")) value->value.i32=s->elements;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b; return x>=y ? x-y<nb : y-x<na;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned frames,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==channels && b->frame_capacity>=frames && (!input || b->frame_count==frames);
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<2 || context->output_count<2) return ORPHEUS_ERR_INVALID_ARG;
    const ComplexGainSplitState* s=(const ComplexGainSplitState*)state;
    const OrpheusBuffer* in=context->inputs[0]; const OrpheusBuffer* gain=context->inputs[1];
    if (!in || !gain) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    if (!s->channels || !valid(in,s->channels,s->elements*2,true) || !valid(gain,s->channels,s->elements,true))
        return ORPHEUS_ERR_INVALID_ARG;
    const size_t bytes=(size_t)s->elements*s->channels*2*sizeof(float);
    for (unsigned i=0;i<2;++i) {
        OrpheusBuffer* out=context->outputs[i];
        if (!out) continue;
        if (!valid(out,s->channels,s->elements*2,false) || overlap(in->data,bytes,out->data,bytes) ||
            overlap(gain->data,bytes/2,out->data,bytes)) return ORPHEUS_ERR_INVALID_ARG;
        if (i && context->outputs[0] && overlap(context->outputs[0]->data,bytes,out->data,bytes))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    OrpheusBuffer* impulse=context->outputs[0]; OrpheusBuffer* reverb=context->outputs[1];
    const float* input=(const float*)in->data; const float* gains=(const float*)gain->data;
    float* residual=impulse ? (float*)impulse->data : NULL;
    float* scaled=reverb ? (float*)reverb->data : NULL;
    for (unsigned n=0;n<s->elements;++n) for (unsigned part=0;part<2;++part) for (unsigned c=0;c<s->channels;++c) {
        size_t index=((size_t)n*2+part)*s->channels+c;
        /* 保留原两步float32舍入，不能改成(1-g)*x或融合乘减。 */
        volatile float product=gains[(size_t)n*s->channels+c]*input[index];
        if (scaled) scaled[index]=product;
        if (residual) residual[index]=input[index]-product;
    }
    if (impulse) impulse->frame_count=s->elements*2;
    if (reverb) reverb->frame_count=s->elements*2;
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
