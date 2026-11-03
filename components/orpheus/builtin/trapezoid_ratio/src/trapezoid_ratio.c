#include "orpheus_trapezoid_ratio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
#define DIM(key,label,initial,limit) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=initial}, \
     .min_i32=1,.max_i32=limit,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
    DIM("channels","通道数",1,64),DIM("bins","每批向量行数",129,65536),DIM("batches","顺序批次数",1,64),
#undef DIM
    {.id="alpha",.name="历史系数",.type=ORPHEUS_VALUE_FLOAT,.default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.9f},
     .min_f32=0,.max_f32=1,.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY},
    {.id="epsilon",.name="分母加数",.type=ORPHEUS_VALUE_FLOAT,.default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=2.22044605e-16f},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY},
    {.id="division_mode",.name="比值顺序（0直接除法/1先倒数再乘）",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},.min_i32=0,.max_i32=1,.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
};
#define PORT(key,dir) {.id=key,.direction=dir,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
static const OrpheusPort ports[]={PORT("numerator",ORPHEUS_PORT_INPUT),PORT("denominator",ORPHEUS_PORT_INPUT),PORT("out",ORPHEUS_PORT_OUTPUT)};
#undef PORT
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.trapezoid_ratio",.version="1.1.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=3,.params=params,.param_count=6,.state_size=sizeof(TrapezoidRatioState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(TrapezoidRatioState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(TrapezoidRatioState));
    ((TrapezoidRatioState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state) {
        TrapezoidRatioState* s=(TrapezoidRatioState*)state;free(s->history);s->history=NULL;
        if (s->owns_memory) free(state);
    }
    return ORPHEUS_OK;
}
static bool scalar_valid(const OrpheusValue* value,unsigned p) {
    return value && value->type==ORPHEUS_VALUE_FLOAT && isfinite(value->value.f32) &&
        value->value.f32>=0 && (p==4 || value->value.f32<=1);
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t dimensions[]={1,129,1},division_mode=0;float values[]={0.9f,2.22044605e-16f};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;while(p<6 && strcmp(key,params[p].id)) ++p;
        if (p==6) return ORPHEUS_ERR_INVALID_ARG;
        if (p<3) {
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
                return ORPHEUS_ERR_INVALID_ARG;
            dimensions[p]=(uint32_t)value->value.i32;
        } else if (p==5) {
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<0 || value->value.i32>1)
                return ORPHEUS_ERR_INVALID_ARG;
            division_mode=(uint32_t)value->value.i32;
        } else {
            if (!scalar_valid(value,p)) return ORPHEUS_ERR_INVALID_ARG;
            values[p-3]=value->value.f32;
        }
    }
    float* history=(float*)calloc((size_t)dimensions[0]*dimensions[1]*4,sizeof(float));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    TrapezoidRatioState* s=(TrapezoidRatioState*)state;free(s->history);s->history=history;
    s->channels=dimensions[0];s->bins=dimensions[1];s->batches=dimensions[2];
    s->alpha=values[0];s->epsilon=values[1];s->division_mode=division_mode;return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    TrapezoidRatioState* s=(TrapezoidRatioState*)state;
    if (s->history) memset(s->history,0,(size_t)s->channels*s->bins*4*sizeof(float));
    return ORPHEUS_OK;
}
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key) return ORPHEUS_ERR_INVALID_ARG;
    if (!strcmp(key,"division_mode")) {
        if (!value || value->type!=ORPHEUS_VALUE_INT || value->value.i32<0 || value->value.i32>1)
            return ORPHEUS_ERR_INVALID_ARG;
        ((TrapezoidRatioState*)state)->division_mode=(uint32_t)value->value.i32;
        return ORPHEUS_OK;
    }
    unsigned p=!strcmp(key,"alpha") ? 3 : !strcmp(key,"epsilon") ? 4 : 5;
    if (p==5) return ORPHEUS_ERR_UNSUPPORTED;
    if (!scalar_valid(value,p)) return ORPHEUS_ERR_INVALID_ARG;
    if (p==3) ((TrapezoidRatioState*)state)->alpha=value->value.f32;
    else ((TrapezoidRatioState*)state)->epsilon=value->value.f32;
    return ORPHEUS_OK;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const TrapezoidRatioState* s=(const TrapezoidRatioState*)state;
    if (!strcmp(key,"alpha") || !strcmp(key,"epsilon")) {
        value->type=ORPHEUS_VALUE_FLOAT;value->value.f32=!strcmp(key,"alpha") ? s->alpha : s->epsilon;
    } else {
        value->type=ORPHEUS_VALUE_INT;
        if (!strcmp(key,"channels")) value->value.i32=s->channels;
        else if (!strcmp(key,"bins")) value->value.i32=s->bins;
        else if (!strcmp(key,"batches")) value->value.i32=s->batches;
        else if (!strcmp(key,"division_mode")) value->value.i32=s->division_mode;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
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
    TrapezoidRatioState* s=(TrapezoidRatioState*)state;
    if (!s->history) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    unsigned frames=s->bins*s->batches;
    size_t points=(size_t)s->bins*s->channels,bytes=points*s->batches*sizeof(float);
    OrpheusBuffer* out=context->outputs[0];
    if (out && !valid(out,s->channels,frames,false)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned p=0;p<2;++p)
        if (!valid(context->inputs[p],s->channels,frames,true) ||
            (out && overlap(context->inputs[p]->data,out->data,bytes))) return ORPHEUS_ERR_INVALID_ARG;
    const float factor=(1.0f-s->alpha)*0.5f;
    for (unsigned h=0;h<s->batches;++h) for (size_t i=0;i<points;++i) {
        size_t index=h*points+i;float smoothed[2];
        for (unsigned p=0;p<2;++p) {
            float* previous=s->history+p*points*2;float* last=previous+points;
            float current=((const float*)context->inputs[p]->data)[index];
            float pair=current+previous[i];
            volatile float feed=pair*factor,feedback=last[i]*s->alpha;
            smoothed[p]=feed+feedback;previous[i]=current;last[i]=smoothed[p];
        }
        volatile float denominator=smoothed[1]+s->epsilon;
        if (out) {
            if (s->division_mode) {
                volatile float reciprocal=1.0f/denominator;
                ((float*)out->data)[index]=reciprocal*smoothed[0];
            } else ((float*)out->data)[index]=smoothed[0]/denominator;
        }
    }
    if (out) out->frame_count=frames;return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
