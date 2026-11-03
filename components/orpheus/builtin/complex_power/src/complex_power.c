#include "orpheus_complex_power.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
#define INTEGER(key,label,initial,low,high) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=initial}, \
     .min_i32=low,.max_i32=high,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
    INTEGER("channels","通道数",1,1,64),
    INTEGER("bins","每批复数点数",129,1,65536),
    INTEGER("batches","批次数",1,1,64),
#undef INTEGER
    {.id="epsilon",.name="对数偏置",.type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=2.22044605e-16f},.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="power",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="energy",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.complex_power",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=3,.params=params,.param_count=4,.state_size=sizeof(ComplexPowerState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state=config->state_block;memset(*state,0,sizeof(ComplexPowerState));
    } else {
        *state=calloc(1,sizeof(ComplexPowerState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((ComplexPowerState*)*state)->owns_memory=true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((ComplexPowerState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    int32_t values[]={1,129,1};float epsilon=2.22044605e-16f;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(key,"epsilon")) {
            if (value->type!=ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32) || value->value.f32<0)
                return ORPHEUS_ERR_INVALID_ARG;
            epsilon=value->value.f32;
        } else {
            unsigned p=0;while(p<3 && strcmp(key,params[p].id)) ++p;
            if(p==3 || value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
            values[p]=value->value.i32;
        }
    }
    for (unsigned p=0;p<3;++p)
        if (values[p]<params[p].min_i32 || values[p]>params[p].max_i32) return ORPHEUS_ERR_INVALID_ARG;
    ComplexPowerState* s=(ComplexPowerState*)state;
    s->channels=values[0];s->bins=values[1];s->batches=values[2];s->epsilon=epsilon;
    return ORPHEUS_OK;
}
static int reset(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    (void)state;(void)key;(void)value;return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const ComplexPowerState* s=(const ComplexPowerState*)state;
    if (!strcmp(key,"epsilon")) {value->type=ORPHEUS_VALUE_FLOAT;value->value.f32=s->epsilon;}
    else {
        value->type=ORPHEUS_VALUE_INT;
        if (!strcmp(key,"channels")) value->value.i32=s->channels;
        else if (!strcmp(key,"bins")) value->value.i32=s->bins;
        else if (!strcmp(key,"batches")) value->value.i32=s->batches;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<nb : y-x<na;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned frames,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==channels && b->frame_capacity>=frames && (!input || b->frame_count==frames);
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<1 || context->output_count<2) return ORPHEUS_ERR_INVALID_ARG;
    const ComplexPowerState* s=(const ComplexPowerState*)state;const OrpheusBuffer* in=context->inputs[0];
    if (!in) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    unsigned frames=s->bins*s->batches;
    if (!s->channels || !valid(in,s->channels,frames*2,true)) return ORPHEUS_ERR_INVALID_ARG;
    unsigned sizes[]={frames,s->batches};
    size_t bytes[]={ (size_t)frames*s->channels*sizeof(float), (size_t)s->batches*s->channels*sizeof(float) };
    for (unsigned i=0;i<2;++i) {
        OrpheusBuffer* out=context->outputs[i];if (!out) continue;
        if (!valid(out,s->channels,sizes[i],false) || overlap(in->data,bytes[0]*2,out->data,bytes[i]))
            return ORPHEUS_ERR_INVALID_ARG;
        if (i && context->outputs[0] && overlap(context->outputs[0]->data,bytes[0],out->data,bytes[i]))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    const float* input=(const float*)in->data;
    OrpheusBuffer* power=context->outputs[0];OrpheusBuffer* energy=context->outputs[1];
    float* powers=power ? (float*)power->data : NULL;float* energies=energy ? (float*)energy->data : NULL;
    for (unsigned h=0;h<s->batches;++h) for (unsigned c=0;c<s->channels;++c) {
        float sum=-0.0f;
        for (unsigned k=0;k<s->bins;++k) {
            size_t point=(size_t)h*s->bins+k,index=point*2*s->channels+c;
            /* 保留原模平方的两次乘法舍入，不融合乘加。 */
            volatile float real=input[index]*input[index];
            volatile float imaginary=input[index+s->channels]*input[index+s->channels];
            float value=real+imaginary;
            if (powers) powers[point*s->channels+c]=value;
            sum+=value;
        }
        if (energies) energies[(size_t)h*s->channels+c]=log10f(sum+s->epsilon)*10.0f;
    }
    if (power) power->frame_count=frames;
    if (energy) energy->frame_count=s->batches;
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
