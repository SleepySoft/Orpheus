#include "orpheus_delta_hold.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
#define DIM(key,label,initial) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=initial}, \
     .min_i32=1,.max_i32=64,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
    DIM("channels","通道数",1),DIM("batches","顺序批次数",1),
#undef DIM
    {.id="threshold",.name="增量阈值",.type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.5f},.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY},
    {.id="hold_ticks",.name="保持批次数",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=18},.min_i32=0,.max_i32=INT32_MAX,
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_I32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.delta_hold",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=params,.param_count=4,.state_size=sizeof(DeltaHoldState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state=config->state_block;memset(*state,0,sizeof(DeltaHoldState));
    } else {
        *state=calloc(1,sizeof(DeltaHoldState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((DeltaHoldState*)*state)->owns_memory=true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((DeltaHoldState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    DeltaHoldState* s=(DeltaHoldState*)state;
    memset(s->previous,0,sizeof(s->previous));memset(s->counter,0,sizeof(s->counter));memset(s->active,0,sizeof(s->active));
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={1,1,18};float threshold=1.5f;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;while(p<4 && strcmp(key,params[p].id)) ++p;
        if (p==4) return ORPHEUS_ERR_INVALID_ARG;
        if (p==2) {
            if (value->type!=ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
            threshold=value->value.f32;
        } else {
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<params[p].min_i32 || value->value.i32>params[p].max_i32)
                return ORPHEUS_ERR_INVALID_ARG;
            values[p==3 ? 2 : p]=(uint32_t)value->value.i32;
        }
    }
    DeltaHoldState* s=(DeltaHoldState*)state;
    s->channels=values[0];s->batches=values[1];s->hold_ticks=values[2];s->threshold=threshold;
    return reset(state);
}
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    DeltaHoldState* s=(DeltaHoldState*)state;
    if (!strcmp(key,"threshold")) {
        if (value->type!=ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        s->threshold=value->value.f32;
    } else if (!strcmp(key,"hold_ticks")) {
        if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<0) return ORPHEUS_ERR_INVALID_ARG;
        s->hold_ticks=(uint32_t)value->value.i32;
    } else return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_OK;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const DeltaHoldState* s=(const DeltaHoldState*)state;
    if (!strcmp(key,"threshold")) {value->type=ORPHEUS_VALUE_FLOAT;value->value.f32=s->threshold;}
    else {
        value->type=ORPHEUS_VALUE_INT;
        if (!strcmp(key,"channels")) value->value.i32=s->channels;
        else if (!strcmp(key,"batches")) value->value.i32=s->batches;
        else if (!strcmp(key,"hold_ticks")) value->value.i32=s->hold_ticks;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned frames,bool input) {
    return b && b->data && b->format==(input ? ORPHEUS_FORMAT_F32 : ORPHEUS_FORMAT_I32) && b->interleaved &&
        b->channels==channels && b->frame_capacity>=frames && (!input || b->frame_count==frames);
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<1 || context->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    DeltaHoldState* s=(DeltaHoldState*)state;
    if (!s->channels || !context->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    const OrpheusBuffer* in=context->inputs[0];OrpheusBuffer* out=context->outputs[0];
    const size_t bytes=(size_t)s->channels*s->batches*sizeof(float);
    if (!valid(in,s->channels,s->batches,true)) return ORPHEUS_ERR_INVALID_ARG;
    if (out) {
        uintptr_t a=(uintptr_t)in->data,b=(uintptr_t)out->data;
        if (!valid(out,s->channels,s->batches,false) || (a>=b ? a-b<bytes : b-a<bytes))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    for (unsigned h=0;h<s->batches;++h) for (unsigned c=0;c<s->channels;++c) {
        size_t i=(size_t)h*s->channels+c;float current=((const float*)in->data)[i];
        volatile float delta=current-s->previous[c];
        int32_t active;
        if (delta>s->threshold) {active=1;s->counter[c]=0;}
        else if (s->active[c]==1 && s->counter[c]<s->hold_ticks) {active=1;++s->counter[c];}
        else active=0;
        s->active[c]=active;s->previous[c]=current;
        if (out) ((int32_t*)out->data)[i]=active;
    }
    if (out) out->frame_count=s->batches;
    return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
