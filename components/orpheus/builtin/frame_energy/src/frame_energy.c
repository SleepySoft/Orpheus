#include "orpheus_frame_energy.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="frames",.name="窗口帧数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=128},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,
     .is_variable=true,.channels_param="channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=1,.block_size=1}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.frame_energy",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=params,.param_count=2,.state_size=sizeof(FrameEnergyState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(FrameEnergyState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(FrameEnergyState));
    ((FrameEnergyState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((FrameEnergyState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={2,128};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=!strcmp(key,"channels") ? 0 : !strcmp(key,"frames") ? 1 : 2;
        if (p==2 || value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
            return ORPHEUS_ERR_INVALID_ARG;
        values[p]=(uint32_t)value->value.i32;
    }
    FrameEnergyState* s=(FrameEnergyState*)state;s->channels=values[0];s->frames=values[1];return ORPHEUS_OK;
}
static int reset(void* state) {return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const FrameEnergyState* s=(const FrameEnergyState*)state;value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"channels")) value->value.i32=(int32_t)s->channels;
    else if (!strcmp(key,"frames")) value->value.i32=(int32_t)s->frames;
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
    if (!state || !context || !context->inputs || !context->outputs || context->input_count<1 || context->output_count<1)
        return ORPHEUS_ERR_INVALID_ARG;
    const FrameEnergyState* s=(const FrameEnergyState*)state;
    if (!s->channels || !s->frames) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    const OrpheusBuffer* in=context->inputs[0];OrpheusBuffer* out=context->outputs[0];
    if (!valid(in,s->channels,s->frames,true)) return ORPHEUS_ERR_INVALID_ARG;
    if (!out) return ORPHEUS_OK;
    if (!valid(out,1,1,false) ||
        overlap(in->data,(size_t)s->channels*s->frames*sizeof(float),out->data,sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    const unsigned char* input=(const unsigned char*)in->data;
    volatile float total=-0.0f;
    for (unsigned frame=0;frame<s->frames;++frame) {
        size_t base=(size_t)frame*s->channels;float value;
        memcpy(&value,input+base*sizeof(float),sizeof(float));
        volatile float row=value*value;
        for (unsigned c=1;c<s->channels;++c) {
            memcpy(&value,input+(base+c)*sizeof(float),sizeof(float));
            volatile float squared=value*value;
            row=row+squared;
        }
        total=total+row;
    }
    float result=total;memcpy(out->data,&result,sizeof(float));out->frame_count=1;return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
